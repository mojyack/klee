#pragma once
#include <array>

#include "context.hpp"

namespace usb::xhci {
union TRB {
    std::array<uint32_t, 4> data;
    struct {
        uint64_t parameter;
        uint32_t status;
        uint32_t cycle_bit : 1;
        uint32_t evaluate_next_trb : 1;
        uint32_t : 8;
        uint32_t trb_type : 6;
        uint32_t control : 16;
    } __attribute__((packed)) bits;
};

union NormalTRB {
    static constexpr auto type = 1;

    std::array<uint32_t, 4> data;
    struct {
        uint64_t data_buffer_pointer;

        uint32_t trb_transfer_length : 17;
        uint32_t td_size : 5;
        uint32_t interrupter_target : 10;

        uint32_t cycle_bit : 1;
        uint32_t evaluate_next_trb : 1;
        uint32_t interrupt_on_short_packet : 1;
        uint32_t no_snoop : 1;
        uint32_t chain_bit : 1;
        uint32_t interrupt_on_completion : 1;
        uint32_t immediate_data : 1;
        uint32_t : 2;
        uint32_t block_event_interrupt : 1;
        uint32_t trb_type : 6;
        uint32_t : 16;
    } __attribute__((packed)) bits;

    auto get_pointer() const -> void* {
        return reinterpret_cast<TRB*>(bits.data_buffer_pointer);
    }

    auto set_pointer(const void* const p) -> void {
        bits.data_buffer_pointer = reinterpret_cast<uint64_t>(p);
    }

    NormalTRB() {
        bits.trb_type = type;
    }
};

union SetupStageTRB {
    static const constexpr auto type           = 2;
    static const constexpr auto no_data_stage  = 0;
    static const constexpr auto out_data_stage = 2;
    static const constexpr auto in_data_stage  = 3;

    std::array<uint32_t, 4> data;
    struct {
        uint32_t request_type : 8;
        uint32_t request : 8;
        uint32_t value : 16;

        uint32_t index : 16;
        uint32_t length : 16;

        uint32_t trb_transfer_length : 17;
        uint32_t : 5;
        uint32_t interrupter_target : 10;

        uint32_t cycle_bit : 1;
        uint32_t : 4;
        uint32_t interrupt_on_completion : 1;
        uint32_t immediate_data : 1;
        uint32_t : 3;
        uint32_t trb_type : 6;
        uint32_t transfer_type : 2;
        uint32_t : 14;
    } __attribute__((packed)) bits;

    SetupStageTRB() {
        bits.trb_type            = type;
        bits.immediate_data      = true;
        bits.trb_transfer_length = 8;
    }
};

union DataStageTRB {
    static constexpr auto type = 3;

    std::array<uint32_t, 4> data;
    struct {
        uint64_t data_buffer_pointer;

        uint32_t trb_transfer_length : 17;
        uint32_t td_size : 5;
        uint32_t interrupter_target : 10;

        uint32_t cycle_bit : 1;
        uint32_t evaluate_next_trb : 1;
        uint32_t interrupt_on_short_packet : 1;
        uint32_t no_snoop : 1;
        uint32_t chain_bit : 1;
        uint32_t interrupt_on_completion : 1;
        uint32_t immediate_data : 1;
        uint32_t : 3;
        uint32_t trb_type : 6;
        uint32_t direction : 1;
        uint32_t : 15;
    } __attribute__((packed)) bits;

    auto get_pointer() const -> void* {
        return reinterpret_cast<void*>(bits.data_buffer_pointer);
    }

    auto set_pointer(const void* const p) -> void {
        bits.data_buffer_pointer = reinterpret_cast<uint64_t>(p);
    }

    DataStageTRB() {
        bits.trb_type = type;
    }
};

union StatusStageTRB {
    static constexpr auto type = 4;

    std::array<uint32_t, 4> data;
    struct {
        uint64_t : 64;

        uint32_t : 22;
        uint32_t interrupter_target : 10;

        uint32_t cycle_bit : 1;
        uint32_t evaluate_next_trb : 1;
        uint32_t : 2;
        uint32_t chain_bit : 1;
        uint32_t interrupt_on_completion : 1;
        uint32_t : 4;
        uint32_t trb_type : 6;
        uint32_t direction : 1;
        uint32_t : 15;
    } __attribute__((packed)) bits;

    StatusStageTRB() {
        bits.trb_type = type;
    }
};

union LinkTRB {
    static constexpr auto type = 6;

    std::array<uint32_t, 4> data;
    struct {
        uint64_t : 4;
        uint64_t ring_segment_pointer : 60;

        uint32_t : 22;
        uint32_t interrupter_target : 10;

        uint32_t cycle_bit : 1;
        uint32_t toggle_cycle : 1;
        uint32_t : 2;
        uint32_t chain_bit : 1;
        uint32_t interrupt_on_completion : 1;
        uint32_t : 4;
        uint32_t trb_type : 6;
        uint32_t : 16;
    } __attribute__((packed)) bits;

    auto get_pointer() const -> TRB* {
        return reinterpret_cast<TRB*>(bits.ring_segment_pointer << 4);
    }

    auto set_pointer(const TRB* const p) -> void {
        bits.ring_segment_pointer = reinterpret_cast<uint64_t>(p) >> 4;
    }

    LinkTRB(const TRB* const ring_segment_pointer) {
        bits.trb_type = type;
        set_pointer(ring_segment_pointer);
    }
};

union NoOpTRB {
    static constexpr auto type = 8;

    std::array<uint32_t, 4> data;
    struct {
        uint64_t : 64;

        uint32_t : 22;
        uint32_t interrupter_target : 10;

        uint32_t cycle_bit : 1;
        uint32_t evaluate_next_trb : 1;
        uint32_t : 2;
        uint32_t chain_bit : 1;
        uint32_t interrupt_on_completion : 1;
        uint32_t : 4;
        uint32_t trb_type : 6;
        uint32_t : 16;
    } __attribute__((packed)) bits;

    NoOpTRB() {
        bits.trb_type = type;
    }
};

union EnableSlotCommandTRB {
    static constexpr auto type = 9;

    std::array<uint32_t, 4> data;
    struct {
        uint32_t : 32;

        uint32_t : 32;

        uint32_t : 32;

        uint32_t cycle_bit : 1;
        uint32_t : 9;
        uint32_t trb_type : 6;
        uint32_t slot_type : 5;
        uint32_t : 11;
    } __attribute__((packed)) bits;

    EnableSlotCommandTRB() {
        bits.trb_type = type;
    }
};

union AddressDeviceCommandTRB {
    static constexpr auto type = 11;

    std::array<uint32_t, 4> data;

    struct {
        uint64_t : 4;
        uint64_t input_context_pointer : 60;

        uint32_t : 32;

        uint32_t cycle_bit : 1;
        uint32_t : 8;
        uint32_t block_set_address_request : 1;
        uint32_t trb_type : 6;
        uint32_t : 8;
        uint32_t slot_id : 8;
    } __attribute__((packed)) bits;

    auto get_pointer() const -> InputContext* {
        return reinterpret_cast<InputContext*>(bits.input_context_pointer << 4);
    }

    auto set_pointer(const InputContext* const p) -> void {
        bits.input_context_pointer = reinterpret_cast<uint64_t>(p) >> 4;
    }

    AddressDeviceCommandTRB(const InputContext* const input_context, const uint8_t slot_id) {
        bits.trb_type = type;
        bits.slot_id  = slot_id;
        set_pointer(input_context);
    }
};

union ConfigureEndpointCommandTRB {
    static constexpr auto type = 12;

    std::array<uint32_t, 4> data;
    struct {
        uint64_t : 4;
        uint64_t input_context_pointer : 60;

        uint32_t : 32;

        uint32_t cycle_bit : 1;
        uint32_t : 8;
        uint32_t deconfigure : 1;
        uint32_t trb_type : 6;
        uint32_t : 8;
        uint32_t slot_id : 8;
    } __attribute__((packed)) bits;

    auto get_pointer() const -> InputContext* {
        return reinterpret_cast<InputContext*>(bits.input_context_pointer << 4);
    }

    void set_pointer(const InputContext* const p) {
        bits.input_context_pointer = reinterpret_cast<uint64_t>(p) >> 4;
    }

    ConfigureEndpointCommandTRB(const InputContext* const input_context, const uint8_t slot_id) {
        bits.trb_type = type;
        bits.slot_id  = slot_id;
        set_pointer(input_context);
    }
};

union StopEndpointCommandTRB {
    static constexpr auto type = 15;

    std::array<uint32_t, 4> data;
    struct {
        uint32_t : 32;

        uint32_t : 32;

        uint32_t : 32;

        uint32_t cycle_bit : 1;
        uint32_t : 9;
        uint32_t trb_type : 6;
        uint32_t endpoint_id : 5;
        uint32_t : 2;
        uint32_t suspend : 1;
        uint32_t slot_id : 8;
    } __attribute__((packed)) bits;

    auto get_endpoint_id() const -> EndpointID {
        return usb::EndpointID{bits.endpoint_id};
    }

    StopEndpointCommandTRB(const EndpointID endpoint_id, const uint8_t slot_id) {
        bits.trb_type    = type;
        bits.endpoint_id = endpoint_id.get_address();
        bits.slot_id     = slot_id;
    }
};

union NoOpCommandTRB {
    static constexpr auto type = 23;

    std::array<uint32_t, 4> data;
    struct {
        uint32_t : 32;

        uint32_t : 32;

        uint32_t : 32;

        uint32_t cycle_bit : 1;
        uint32_t : 9;
        uint32_t trb_type : 6;
        uint32_t : 16;
    } __attribute__((packed)) bits;

    NoOpCommandTRB() {
        bits.trb_type = type;
    }
};

union TransferEventTRB {
    static constexpr auto type = 32;

    std::array<uint32_t, 4> data;
    struct {
        uint64_t trb_pointer : 64;

        uint32_t trb_transfer_length : 24;
        uint32_t completion_code : 8;

        uint32_t cycle_bit : 1;
        uint32_t : 1;
        uint32_t event_data : 1;
        uint32_t : 7;
        uint32_t trb_type : 6;
        uint32_t endpoint_id : 5;
        uint32_t : 3;
        uint32_t slot_id : 8;
    } __attribute__((packed)) bits;

    auto get_pointer() const -> TRB* {
        return reinterpret_cast<TRB*>(bits.trb_pointer);
    }

    auto set_pointer(const TRB* const p) -> void {
        bits.trb_pointer = reinterpret_cast<uint64_t>(p);
    }

    auto get_endpoint_id() const -> EndpointID {
        return usb::EndpointID(bits.endpoint_id);
    }

    TransferEventTRB() {
        bits.trb_type = type;
    }
};

union CommandCompletionEventTRB {
    static constexpr auto type = 33;

    std::array<uint32_t, 4> data;
    struct {
        uint64_t : 4;
        uint64_t command_trb_pointer : 60;

        uint32_t command_completion_parameter : 24;
        uint32_t completion_code : 8;

        uint32_t cycle_bit : 1;
        uint32_t : 9;
        uint32_t trb_type : 6;
        uint32_t vf_id : 8;
        uint32_t slot_id : 8;
    } __attribute__((packed)) bits;

    auto get_pointer() const -> TRB* {
        return reinterpret_cast<TRB*>(bits.command_trb_pointer << 4);
    }

    auto setPointer(TRB* const p) -> void {
        bits.command_trb_pointer = reinterpret_cast<uint64_t>(p) >> 4;
    }

    CommandCompletionEventTRB() {
        bits.trb_type = type;
    }
};

union PortStatusChangeEventTRB {
    static constexpr auto type = 34;

    std::array<uint32_t, 4> data;
    struct {
        uint32_t : 24;
        uint32_t port_id : 8;

        uint32_t : 32;

        uint32_t : 24;
        uint32_t completion_code : 8;

        uint32_t cycle_bit : 1;
        uint32_t : 9;
        uint32_t trb_type : 6;
    } __attribute__((packed)) bits;

    PortStatusChangeEventTRB() {
        bits.trb_type = type;
    }
};

template <class To, class From>
auto trb_dynamic_cast(From* const trb) -> To* {
    if(To::type == trb->bits.trb_type) {
        return reinterpret_cast<To*>(trb);
    }
    return nullptr;
}

constexpr auto trb_completioncode_str = std::array{
    "Invalid",
    "Success",
    "Data Buffer Error",
    "Babble Detected Error",
    "USB Transaction Error",
    "TRB Error",
    "Stall Error",
    "Resource Error",
    "Bandwidth Error",
    "No Slots Available Error",
    "Invalid Stream Type Error",
    "Slot Not Enabled Error",
    "Endpoint Not Enabled Error",
    "Short Packet",
    "Ring Underrun",
    "Ring Overrun",
    "VF Event Ring Full Error",
    "Parameter Error",
    "Bandwidth Overrun Error",
    "Context State Error",
    "No ping Response Error",
    "Event Ring Full Error",
    "Incompatible Device Error",
    "Missed Service Error",
    "Command Ring Stopped",
    "Command Aborted",
    "Stopped",
    "Stopped - Length Invalid",
    "Stopped - Short Packet",
    "Max Exit Latency Too Large Error",
    "Reserved",
    "Isoch Buffer Overrun",
    "Event Lost Error",
    "Undefined Error",
    "Invalid Stream ID Error",
    "Secondary Bandwidth Error",
    "Split Transaction Error",
};

constexpr auto trb_type_str = std::array{
    "Reserved", // 0
    "Normal",
    "Setup Stage",
    "Data Stage",
    "Status Stage",
    "Isoch",
    "Link",
    "EventData",
    "No-Op", // 8
    "Enable Slot Command",
    "Disable Slot Command",
    "Address Device Command",
    "Configure Endpoint Command",
    "Evaluate Context Command",
    "Reset Endpoint Command",
    "Stop Endpoint Command",
    "Set TR Dequeue Pointer Command", // 16
    "Reset Device Command",
    "Force Event Command",
    "Negotiate Bandwidth Command",
    "Set Latency Tolerance Value Command",
    "Get Port Bandwidth Command",
    "Force Header Command",
    "No Op Command",
    "Reserved", // 24
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Transfer Event", // 32
    "Command Completion Event",
    "Port Status Change Event",
    "Bandwidth Request Event",
    "Doorbell Event",
    "Host Controller Event",
    "Device Notification Event",
    "MFINDEX Wrap Event",
    "Reserved", // 40
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Vendor Defined", // 48
    "Vendor Defined",
    "Vendor Defined",
    "Vendor Defined",
    "Vendor Defined",
    "Vendor Defined",
    "Vendor Defined",
    "Vendor Defined",
    "Vendor Defined", // 56
    "Vendor Defined",
    "Vendor Defined",
    "Vendor Defined",
    "Vendor Defined",
    "Vendor Defined",
    "Vendor Defined",
    "Vendor Defined",
};
} // namespace usb::xhci
