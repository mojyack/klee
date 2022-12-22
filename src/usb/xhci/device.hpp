#pragma once
#include <cstddef>
#include <cstdint>

#include "../../error.hpp"
#include "../../log.hpp"
#include "../device.hpp"
#include "context.hpp"
#include "registers.hpp"
#include "ring.hpp"
#include "trb.hpp"

#include "../../print.hpp"

namespace usb::xhci {
class Device final : public usb::Device {
  public:
    enum class State {
        Invalid,
        Blank,
        SlotAssigning,
        SlotAssigned
    };

  private:
    alignas(64) DeviceContext context;
    alignas(64) InputContext input_context;

    uint8_t               slot_id;
    DoorbellRegister*     doorbell_register;
    State                 state;
    std::array<Ring*, 31> transfer_rings;

    std::unordered_map<const void*, const SetupStageTRB*> setup_stage_map;

    static auto make_setupstage_trb(const SetupData setup_data, const int transfer_type) -> SetupStageTRB {
        auto setup               = SetupStageTRB();
        setup.bits.request_type  = setup_data.request_type.data;
        setup.bits.request       = setup_data.request;
        setup.bits.value         = setup_data.value;
        setup.bits.index         = setup_data.index;
        setup.bits.length        = setup_data.length;
        setup.bits.transfer_type = transfer_type;
        return setup;
    }

    static auto make_datastage_trb(const void* const buf, const int len, const bool dir_in) -> DataStageTRB {
        auto data = DataStageTRB();
        data.set_pointer(buf);
        data.bits.trb_transfer_length = len;
        data.bits.td_size             = 0;
        data.bits.direction           = dir_in;
        return data;
    }

  public:
    using OnTransferredCallbackType = void(Device* dev, DeviceContextIndex dci, int completion_code, int trb_transfer_length, TRB* issue_trb);

    auto initialize() -> Error {
        state = State::Blank;
        return Error::Code::Success;
    }

    auto get_device_context() -> DeviceContext* {
        return &context;
    }

    auto get_input_context() -> InputContext* {
        return &input_context;
    }

    auto get_state() const -> State {
        return state;
    }

    auto get_slot_id() const -> uint8_t {
        return slot_id;
    }

    auto select_for_slot_assignment() -> void {
        state = State::SlotAssigning;
    }

    auto allocate_transfer_ring(const DeviceContextIndex index, const size_t buffer_count) -> Ring* {
        const auto i  = index.value - 1;
        const auto tr = new(std::nothrow) Ring;
        if(tr != nullptr) {
            tr->initialize(buffer_count);
        }
        transfer_rings[i] = tr;
        return tr;
    }

    auto control_in(const EndpointID id, const SetupData setup_data, void* const buf, const int len, ClassDriver* const issuer) -> Error override {
        if(const auto error = usb::Device::control_in(id, setup_data, buf, len, issuer)) {
            return error;
        }

        if(id.get_number() < 0 || 15 < id.get_number()) {
            return Error::Code::InvalidEndpointNumber;
        }

        // control endpoint must be dir_in=true
        const auto dci = DeviceContextIndex(id);
        const auto tr  = transfer_rings[dci.value - 1];
        if(tr == nullptr) {
            return Error::Code::TransferRingNotSet;
        }

        auto status = StatusStageTRB();

        if(buf != nullptr) {
            const auto setup_trb_position     = trb_dynamic_cast<SetupStageTRB>(tr->push(make_setupstage_trb(setup_data, SetupStageTRB::in_data_stage)));
            auto       data                   = make_datastage_trb(buf, len, true);
            data.bits.interrupt_on_completion = true;
            const auto data_trb_position      = tr->push(data);
            tr->push(status);

            setup_stage_map[data_trb_position] = setup_trb_position;
        } else {
            const auto setup_trb_position       = trb_dynamic_cast<SetupStageTRB>(tr->push(make_setupstage_trb(setup_data, SetupStageTRB::no_data_stage)));
            status.bits.direction               = true;
            status.bits.interrupt_on_completion = true;
            const auto status_trb_position      = tr->push(status);

            setup_stage_map[status_trb_position] = setup_trb_position;
        }

        doorbell_register->ring(dci.value);

        return Error::Code::Success;
    }

    auto control_out(const EndpointID id, const SetupData setup_data, void* const buf, const int len, ClassDriver* const issuer) -> Error override {
        if(auto err = usb::Device::control_out(id, setup_data, buf, len, issuer)) {
            return err;
        }

        if(id.get_number() < 0 || 15 < id.get_number()) {
            return Error::Code::InvalidEndpointNumber;
        }

        // control endpoint must be dir_in=true
        const auto dci = DeviceContextIndex(id);
        const auto tr  = transfer_rings[dci.value - 1];
        if(tr == nullptr) {
            return Error::Code::TransferRingNotSet;
        }

        auto status           = StatusStageTRB();
        status.bits.direction = true;

        if(buf != nullptr) {
            const auto setup_trb_position     = trb_dynamic_cast<SetupStageTRB>(tr->push(make_setupstage_trb(setup_data, SetupStageTRB::out_data_stage)));
            auto       data                   = make_datastage_trb(buf, len, false);
            data.bits.interrupt_on_completion = true;
            const auto data_trb_position      = tr->push(data);
            tr->push(status);

            setup_stage_map[data_trb_position] = setup_trb_position;
        } else {
            const auto setup_trb_position       = trb_dynamic_cast<SetupStageTRB>(tr->push(make_setupstage_trb(setup_data, SetupStageTRB::no_data_stage)));
            status.bits.interrupt_on_completion = true;
            const auto status_trb_position      = tr->push(status);

            setup_stage_map[status_trb_position] = setup_trb_position;
        }

        doorbell_register->ring(dci.value);

        return Error::Code::Success;
    }

    auto interrupt_in(const EndpointID id, void* const buf, const int len) -> Error override {
        if(const auto error = usb::Device::interrupt_in(id, buf, len)) {
            return error;
        }

        const auto dci = DeviceContextIndex(id);
        const auto tr  = transfer_rings[dci.value - 1];
        if(tr == nullptr) {
            return Error::Code::TransferRingNotSet;
        }

        auto normal = NormalTRB();
        normal.set_pointer(buf);
        normal.bits.trb_transfer_length       = len;
        normal.bits.interrupt_on_short_packet = true;
        normal.bits.interrupt_on_completion   = true;

        tr->push(normal);
        doorbell_register->ring(dci.value);
        return Error::Code::Success;
    }

    auto interrupt_out(const EndpointID id, void* const buf, const int len) -> Error override {
        if(const auto error = usb::Device::interrupt_out(id, buf, len)) {
            return error;
        }

        return Error::Code::NotImplemented;
    }

    auto on_transfer_event_received(const TransferEventTRB& trb) -> Error {
        const auto residual_length = trb.bits.trb_transfer_length;

        if(trb.bits.completion_code != 1 /* Success */ &&
           trb.bits.completion_code != 13 /* Short Packet */) {
            return Error::Code::TransferFailed;
        }

        const auto issuer_trb = trb.get_pointer();
        if(const auto normal_trb = trb_dynamic_cast<NormalTRB>(issuer_trb)) {
            const auto transfer_length = normal_trb->bits.trb_transfer_length - residual_length;
            return on_interrupt_completed(trb.get_endpoint_id(), normal_trb->get_pointer(), transfer_length);
        }

        const auto opt_setup_stage_trb = setup_stage_map.find(issuer_trb);
        if(opt_setup_stage_trb == setup_stage_map.end()) {
            if(const auto data_trb = trb_dynamic_cast<DataStageTRB>(issuer_trb)) {
                logger(LogLevel::Error, "usb::xhci: no corresponding setup stage %d", data_trb->type);
            }
            return Error::Code::NoCorrespondingSetupStage;
        }
        setup_stage_map.erase(issuer_trb);

        const auto setup_stage_trb   = opt_setup_stage_trb->second;
        auto       setup_data        = SetupData();
        setup_data.request_type.data = setup_stage_trb->bits.request_type;
        setup_data.request           = setup_stage_trb->bits.request;
        setup_data.value             = setup_stage_trb->bits.value;
        setup_data.index             = setup_stage_trb->bits.index;
        setup_data.length            = setup_stage_trb->bits.length;

        auto data_stage_buffer = (void*)(nullptr);
        auto transfer_length   = 0;
        if(const auto data_stage_trb = trb_dynamic_cast<DataStageTRB>(issuer_trb)) {
            data_stage_buffer = data_stage_trb->get_pointer();
            transfer_length   = data_stage_trb->bits.trb_transfer_length - residual_length;
        } else if(const auto status_stage_trb = trb_dynamic_cast<StatusStageTRB>(issuer_trb)) {
            // pass
        } else {
            return Error::Code::NotImplemented;
        }
        return on_control_completed(trb.get_endpoint_id(), setup_data, data_stage_buffer, transfer_length);
    }

    Device(const uint8_t slot_id, DoorbellRegister* const doorbell_register) : slot_id(slot_id), doorbell_register(doorbell_register) {}
};
} // namespace usb::xhci
