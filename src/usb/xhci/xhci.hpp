#pragma once
#include <cstdint>

#include "device-manager.hpp"
#include "port.hpp"
#include "registers.hpp"
#include "speed.hpp"

namespace usb::xhci {
namespace internal {
enum class ConfigPhase {
    NotConnected,
    WaitingAddressed,
    ResettingPort,
    EnablingSlot,
    AddressingDevice,
    InitializingDevice,
    ConfiguringEndpoints,
    Configured,
};

inline auto register_command_ring(Ring* const ring, MemoryMappedRegister<CRCRBitmap>* const crcr) -> Error {
    auto value                  = crcr->read();
    value.bits.ring_cycle_state = true;
    value.bits.command_stop     = false;
    value.bits.command_abort    = false;
    value.set_pointer(reinterpret_cast<uint64_t>(ring->get_buffer()));
    crcr->write(value);
    return Error::Code::Success;
}

inline auto port_config_phase = std::array<volatile ConfigPhase, 256>(); // index: port number
inline auto addressing_port   = 0;

inline auto initialize_slot_context(SlotContext& ctx, Port& port) -> void {
    ctx.bits.route_string      = 0;
    ctx.bits.root_hub_port_num = port.get_number();
    ctx.bits.context_entries   = 1;
    ctx.bits.speed             = port.get_speed();
}

inline auto determine_max_packet_size_for_control_pipe(const unsigned int slot_speed) -> unsigned int {
    switch(slot_speed) {
    case 4: // Super Speed
        return 512;
    case 3: // High Speed
        return 64;
    default:
        return 8;
    }
}

inline auto calc_most_significant_bit(const uint32_t value) -> int {
    if(value == 0) {
        return -1;
    }

    auto msb_index = int();
    __asm__("bsr %1, %0"
            : "=r"(msb_index)
            : "m"(value));
    return msb_index;
}

inline auto initialize_ep0_context(EndpointContext& ctx, Ring* const transfer_ring, const unsigned int max_packet_size) -> void {
    ctx.bits.ep_type         = 4; // Control Endpoint. Bidirectional.
    ctx.bits.max_packet_size = max_packet_size;
    ctx.bits.max_burst_size  = 0;
    ctx.set_transfer_ring_buffer(transfer_ring->get_buffer());
    ctx.bits.dequeue_cycle_state = 1;
    ctx.bits.interval            = 0;
    ctx.bits.max_primary_streams = 0;
    ctx.bits.mult                = 0;
    ctx.bits.error_count         = 3;
}

inline auto reset_port(Port& port) -> Error {
    const auto is_connected = port.is_connected();
    if(!is_connected) {
        return Error::Code::Success;
    }

    if(addressing_port != 0) {
        port_config_phase[port.get_number()] = ConfigPhase::WaitingAddressed;
    } else {
        const auto port_phase = port_config_phase[port.get_number()];
        if(port_phase != ConfigPhase::NotConnected && port_phase != ConfigPhase::WaitingAddressed) {
            return Error::Code::InvalidPhase;
        }
        addressing_port                      = port.get_number();
        port_config_phase[port.get_number()] = ConfigPhase::ResettingPort;
        if(const auto e = port.reset()) {
            return e;
        }
    }
    return Error::Code::Success;
}

inline auto request_hc_ownership(const uintptr_t mmio_base, const HCCPARAMS1Bitmap hccp) -> void {
    const auto extregs = ExtendedRegisterList(mmio_base, hccp);

    const auto ext_usblegsup = std::find_if(extregs.begin(), extregs.end(), [](auto& reg) { return reg.read().bits.capability_id == 1; });

    if(ext_usblegsup == extregs.end()) {
        return;
    }

    auto& reg = reinterpret_cast<MemoryMappedRegister<USBLEGSUPBitmap>&>(*ext_usblegsup);
    auto  r   = reg.read();
    if(r.bits.hc_os_owned_semaphore) {
        return;
    }

    r.bits.hc_os_owned_semaphore = 1;
    reg.write(r);

    do {
        r = reg.read();
    } while(r.bits.hc_bios_owned_semaphore || !r.bits.hc_os_owned_semaphore);
}
} // namespace internal

class Controller {
  private:
    static constexpr auto device_size = 8;

    const uintptr_t       mmio_base;
    CapabilityRegisters*  cap;
    OperationalRegisters* op;
    uint8_t               max_ports;
    DeviceManager         device_manager;
    Ring                  cr;
    EventRing             er;

    auto get_interrupter_register_sets() const -> InterrupterRegisterSetArray {
        return {mmio_base + cap->rtsoff.read().get_offset() + 0x20u, 1024};
    }

    auto get_port_register_sets() const -> PortRegisterSetArray {
        return {reinterpret_cast<uintptr_t>(op) + 0x0400u, max_ports};
    }

    auto get_doorbell_registers() const -> DoorbellRegisterArray {
        return {mmio_base + cap->dboff.read().get_offset(), 256};
    }

    auto enable_slot(Port& port) -> Error {
        const auto is_enabled         = port.is_enabled();
        const auto is_reset_completed = port.is_port_reset_changed();
        if(is_enabled && is_reset_completed) {
            port.clear_port_reset_change();

            internal::port_config_phase[port.get_number()] = internal::ConfigPhase::EnablingSlot;

            auto cmd = EnableSlotCommandTRB();
            get_command_ring()->push(cmd);
            get_doorbell_register_at(0)->ring(0);
        }
        return Error::Code::Success;
    }

    auto address_device(const uint8_t port_id, const uint8_t slot_id) -> Error {
        if(const auto e = device_manager.allocate_device(slot_id, get_doorbell_register_at(slot_id))) {
            return e;
        }

        const auto dev = device_manager.find_by_slot(slot_id);
        if(dev == nullptr) {
            return Error::Code::InvalidSlotID;
        }

        memset(&dev->get_input_context()->input_control_context, 0, sizeof(InputControlContext));

        const auto ep0_dci  = DeviceContextIndex(0, false);
        const auto slot_ctx = dev->get_input_context()->enable_slot_context();
        const auto ep0_ctx  = dev->get_input_context()->enable_end_point(ep0_dci);

        auto port = get_port_at(port_id);
        internal::initialize_slot_context(*slot_ctx, port);

        const auto transfer_ring = dev->allocate_transfer_ring(ep0_dci, 32);
        if(!transfer_ring) {
            return transfer_ring.as_error();
        }
        internal::initialize_ep0_context(*ep0_ctx, transfer_ring.as_value(), internal::determine_max_packet_size_for_control_pipe(slot_ctx->bits.speed));

        if(const auto e = device_manager.load_dcbaa(slot_id)) {
            return e;
        }

        internal::port_config_phase[port_id] = internal::ConfigPhase::AddressingDevice;

        auto addr_dev_cmd = AddressDeviceCommandTRB(dev->get_input_context(), slot_id);
        cr.push(addr_dev_cmd);
        get_doorbell_register_at(0)->ring(0);

        return Error::Code::Success;
    }

    auto initialize_device(const uint8_t port_id, const uint8_t slot_id) -> Error {
        const auto dev = device_manager.find_by_slot(slot_id);
        if(dev == nullptr) {
            return Error::Code::InvalidSlotID;
        }

        internal::port_config_phase[port_id] = internal::ConfigPhase::InitializingDevice;
        if(const auto e = dev->start_initializing()) {
            return e;
        }

        return Error::Code::Success;
    }

    auto complete_configuration(const uint8_t port_id, const uint8_t slot_id) -> Error {
        const auto dev = device_manager.find_by_slot(slot_id);
        if(dev == nullptr) {
            return Error::Code::InvalidSlotID;
        }

        if(const auto e = dev->on_endpoint_configured()) {
            return e;
        }

        internal::port_config_phase[port_id] = internal::ConfigPhase::Configured;
        return Error::Code::Success;
    }

    auto on_event(PortStatusChangeEventTRB& trb) -> Error {
        auto port_id = trb.bits.port_id;
        auto port    = get_port_at(port_id);

        switch(internal::port_config_phase[port_id]) {
        case internal::ConfigPhase::NotConnected:
            return internal::reset_port(port);
        case internal::ConfigPhase::ResettingPort:
            return enable_slot(port);
        default:
            return Error::Code::InvalidPhase;
        }
    }

    auto on_event(TransferEventTRB& trb) -> Error {
        const auto slot_id = trb.bits.slot_id;
        const auto dev     = device_manager.find_by_slot(slot_id);

        if(dev == nullptr) {
            return Error::Code::InvalidSlotID;
        }
        if(const auto error = dev->on_transfer_event_received(trb)) {
            return error;
        }

        const auto port_id = dev->get_device_context()->slot_context.bits.root_hub_port_num;
        if(dev->is_initialized() && internal::port_config_phase[port_id] == internal::ConfigPhase::InitializingDevice) {
            return configure_endpoints(*dev);
        }
        return Error::Code::Success;
    }

    auto on_event(CommandCompletionEventTRB& trb) -> Error {
        const auto issuer_type = trb.get_pointer()->bits.trb_type;
        const auto slot_id     = trb.bits.slot_id;

        if(issuer_type == EnableSlotCommandTRB::type) {
            if(internal::port_config_phase[internal::addressing_port] != internal::ConfigPhase::EnablingSlot) {
                return Error::Code::InvalidPhase;
            }

            return address_device(internal::addressing_port, slot_id);
        } else if(issuer_type == AddressDeviceCommandTRB::type) {
            const auto dev = device_manager.find_by_slot(slot_id);
            if(dev == nullptr) {
                return Error::Code::InvalidSlotID;
            }

            const auto port_id = dev->get_device_context()->slot_context.bits.root_hub_port_num;

            if(port_id != internal::addressing_port) {
                return Error::Code::InvalidPhase;
            }
            if(internal::port_config_phase[port_id] != internal::ConfigPhase::AddressingDevice) {
                return Error::Code::InvalidPhase;
            }

            internal::addressing_port = 0;
            for(auto i = 0; i < internal::port_config_phase.size(); i += 1) {
                if(internal::port_config_phase[i] == internal::ConfigPhase::WaitingAddressed) {
                    auto port = get_port_at(i);
                    if(const auto error = internal::reset_port(port)) {
                        return error;
                    }
                    break;
                }
            }

            return initialize_device(port_id, slot_id);
        } else if(issuer_type == ConfigureEndpointCommandTRB::type) {
            const auto dev = device_manager.find_by_slot(slot_id);
            if(dev == nullptr) {
                return Error::Code::InvalidSlotID;
            }

            auto port_id = dev->get_device_context()->slot_context.bits.root_hub_port_num;
            if(internal::port_config_phase[port_id] != internal::ConfigPhase::ConfiguringEndpoints) {
                return Error::Code::InvalidPhase;
            }

            return complete_configuration(port_id, slot_id);
        }

        return Error::Code::InvalidPhase;
    }

  public:
    auto initialize() -> Error {
        if(const auto error = device_manager.initialize(device_size)) {
            return error;
        }

        internal::request_hc_ownership(mmio_base, cap->hccparams1.read());
        auto usbcmd                          = op->usbcmd.read();
        usbcmd.bits.interrupter_enable       = false;
        usbcmd.bits.host_system_error_enable = false;
        usbcmd.bits.enable_wrap_event        = false;

        // host controller must be halted before resetting it
        if(!op->usbsts.read().bits.host_controller_halted) {
            usbcmd.bits.run_stop = false; // stop
        }

        op->usbcmd.write(usbcmd);
        while(!op->usbsts.read().bits.host_controller_halted) {
            //
        }

        // reset controller
        usbcmd                            = op->usbcmd.read();
        usbcmd.bits.host_controller_reset = true;
        op->usbcmd.write(usbcmd);
        while(op->usbcmd.read().bits.host_controller_reset) {
            //
        }
        while(op->usbsts.read().bits.controller_not_ready) {
            //
        }

        // set "Max Slots Enabled" field in CONFIG
        auto config                          = op->config.read();
        config.bits.max_device_slots_enabled = device_size;
        op->config.write(config);

        auto       hcsparams2             = cap->hcsparams2.read();
        const auto max_scratchpad_buffers = hcsparams2.bits.max_scratchpad_buffers_low | (hcsparams2.bits.max_scratchpad_buffers_high << 5);
        if(max_scratchpad_buffers > 0) {
            auto scratchpad_buf_arr = std::unique_ptr<void*>(new(std::align_val_t{64}, std::nothrow) void*[max_scratchpad_buffers]);
            if(!scratchpad_buf_arr) {
                return Error::Code::NoEnoughMemory;
            }
            for(auto i = 0; i < max_scratchpad_buffers; i += 1) {
                const auto buf = new(std::align_val_t{4096}, std::nothrow) std::byte[4096];
                if(buf == nullptr) {
                    return Error::Code::NoEnoughMemory;
                }
                scratchpad_buf_arr.get()[i] = buf;
            }
            device_manager.get_device_contexts()[0] = std::bit_cast<DeviceContext*>(scratchpad_buf_arr.release());
        }

        auto dcbaap = DCBAAPBitmap();
        dcbaap.set_pointer(reinterpret_cast<uint64_t>(device_manager.get_device_contexts()));
        op->dcbaap.write(dcbaap);

        auto primary_interrupter = &get_interrupter_register_sets()[0];
        if(const auto error = cr.initialize(32)) {
            return error;
        }
        if(const auto error = internal::register_command_ring(&cr, &op->crcr)) {
            return error;
        }
        if(const auto error = er.initialize(32, primary_interrupter)) {
            return error;
        }

        // enable interrupt for the primary interrupter
        auto iman                   = primary_interrupter->iman.read();
        iman.bits.interrupt_pending = true;
        iman.bits.interrupt_enable  = true;
        primary_interrupter->iman.write(iman);

        // enable interrupt for the controller
        usbcmd                         = op->usbcmd.read();
        usbcmd.bits.interrupter_enable = true;
        op->usbcmd.write(usbcmd);

        return Error::Code::Success;
    }

    auto run() -> Error {
        auto usbcmd          = op->usbcmd.read();
        usbcmd.bits.run_stop = true;
        op->usbcmd.write(usbcmd);

        while(op->usbsts.read().bits.host_controller_halted) {
            //
        }

        return Error::Code::Success;
    }

    auto get_command_ring() -> Ring* {
        return &cr;
    }

    auto get_doorbell_register_at(const uint8_t index) -> DoorbellRegister* {
        return &get_doorbell_registers()[index];
    }

    auto get_port_at(const uint8_t port_number) const -> Port {
        return Port(port_number, get_port_register_sets()[port_number - 1]);
    }

    auto get_max_ports() const -> uint8_t {
        return max_ports;
    }

    auto get_device_manager() -> DeviceManager* {
        return &device_manager;
    }

    auto configure_port(Port& port) -> Error {
        if(internal::port_config_phase[port.get_number()] == internal::ConfigPhase::NotConnected) {
            return internal::reset_port(port);
        }
        return Error::Code::Success;
    }

    auto configure_endpoints(Device& dev) -> Error {
        const auto [len, configs] = dev.get_endpoint_configs();

        memset(&dev.get_input_context()->input_control_context, 0, sizeof(InputControlContext));
        memcpy(&dev.get_input_context()->slot_context, &dev.get_device_context()->slot_context, sizeof(SlotContext));

        auto slot_ctx                  = dev.get_input_context()->enable_slot_context();
        slot_ctx->bits.context_entries = 31;
        const auto port_id             = dev.get_device_context()->slot_context.bits.root_hub_port_num;
        const auto port_speed          = get_port_at(port_id).get_speed();
        if(port_speed == 0 || port_speed > static_cast<int>(Speed::SuperPlus)) {
            return Error::Code::UnknownXHCISpeedID;
        }

        auto convert_interval =
            (port_speed == static_cast<int>(Speed::Full) || port_speed == static_cast<int>(Speed::Low)) ? [](const EndpointType type, const int interval) { // for FS, LS
                return type == EndpointType::Isochronous ? interval + 2 : internal::calc_most_significant_bit(interval) + 3;
            }
                                                                                                        : [](const EndpointType type, const int interval) { // for HS, SS, SSP
                                                                                                              return interval - 1;
                                                                                                          };

        for(auto i = 0; i < len; i += 1) {
            const auto ep_dci = DeviceContextIndex(configs[i].id);
            const auto ep_ctx = dev.get_input_context()->enable_end_point(ep_dci);
            switch(configs[i].type) {
            case EndpointType::Control:
                ep_ctx->bits.ep_type = 4;
                break;
            case EndpointType::Isochronous:
                ep_ctx->bits.ep_type = configs[i].id.is_in() ? 5 : 1;
                break;
            case EndpointType::Bulk:
                ep_ctx->bits.ep_type = configs[i].id.is_in() ? 6 : 2;
                break;
            case EndpointType::Interrupt:
                ep_ctx->bits.ep_type = configs[i].id.is_in() ? 7 : 3;
                break;
            }
            ep_ctx->bits.max_packet_size    = configs[i].max_packet_size;
            ep_ctx->bits.interval           = convert_interval(configs[i].type, configs[i].interval);
            ep_ctx->bits.average_trb_length = 1;

            const auto tr = dev.allocate_transfer_ring(ep_dci, 32);
            if(!tr) {
                return tr.as_error();
            }
            ep_ctx->set_transfer_ring_buffer(tr.as_value()->get_buffer());

            ep_ctx->bits.dequeue_cycle_state = 1;
            ep_ctx->bits.max_primary_streams = 0;
            ep_ctx->bits.mult                = 0;
            ep_ctx->bits.error_count         = 3;
        }

        internal::port_config_phase[port_id] = internal::ConfigPhase::ConfiguringEndpoints;

        const auto cmd = ConfigureEndpointCommandTRB(dev.get_input_context(), dev.get_slot_id());
        cr.push(cmd);
        get_doorbell_register_at(0)->ring(0);

        return Error::Code::Success;
    }

    auto has_unprocessed_event() const -> bool {
        return er.has_front();
    }

    auto process_event() -> Error {
        if(!has_unprocessed_event()) {
            return Error::Code::Success;
        }

        auto       error     = Error(Error::Code::NotImplemented);
        const auto event_trb = er.get_front();
        if(const auto trb = trb_dynamic_cast<TransferEventTRB>(event_trb)) {
            error = on_event(*trb);
        } else if(const auto trb = trb_dynamic_cast<PortStatusChangeEventTRB>(event_trb)) {
            error = on_event(*trb);
        } else if(const auto trb = trb_dynamic_cast<CommandCompletionEventTRB>(event_trb)) {
            error = on_event(*trb);
        }
        er.pop();
        return error;
    }

    Controller(const uintptr_t mmio_base) : mmio_base(mmio_base),
                                            cap(reinterpret_cast<CapabilityRegisters*>(mmio_base)),
                                            op(reinterpret_cast<OperationalRegisters*>(mmio_base + cap->caplength.read())),
                                            max_ports{static_cast<uint8_t>(cap->hcsparams1.read().bits.max_ports)} {}
};
} // namespace usb::xhci
