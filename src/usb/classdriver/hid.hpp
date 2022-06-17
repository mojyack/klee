#pragma once
#include "../device.hpp"
#include "hid-decl.hpp"

namespace usb {
inline auto HIDBaseDriver::initialize() -> Error {
    return Error::Code::NotImplemented;
}

inline auto HIDBaseDriver::set_endpoint(const EndpointConfig& config) -> Error {
    if(config.type == EndpointType::Interrupt) {
        (config.id.is_in() ? interrupt_in : interrupt_out) = config.id;
    }
    return Error::Code::Success;
}

inline auto HIDBaseDriver::on_endpoint_configured() -> Error {
    auto setup_data = SetupData();

    setup_data.request_type.bits.direction = static_cast<uint8_t>(DirectionRequestType::Out);
    setup_data.request_type.bits.type      = static_cast<uint8_t>(TypeRequestType::Class);
    setup_data.request_type.bits.recipient = static_cast<uint8_t>(RecipientRequestType::Interface);
    setup_data.request                     = static_cast<uint8_t>(Request::SetProtocol);
    setup_data.value                       = 0; // boot protocol
    setup_data.index                       = interface_index;
    setup_data.length                      = 0;

    initialize_phase = 1;
    return get_owner_device()->control_out(default_control_pipe_id, setup_data, nullptr, 0, this);
}

inline auto HIDBaseDriver::on_control_completed(const EndpointID id, const SetupData& setup_data, const void* const buf, const int len) -> Error {
    if(initialize_phase != 1) {
        return Error::Code::NotImplemented;
    }

    initialize_phase = 2;
    return get_owner_device()->interrupt_in(interrupt_in, buffer.data(), buffer_size);
}

inline auto HIDBaseDriver::on_interrupt_completed(const EndpointID id, const void* const buf, const int len) -> Error {
    if(!id.is_in()) {
        return Error::Code::NotImplemented;
    }
    on_data_received();
    std::copy_n(buffer.begin(), len, prev_buffer.begin());
    return get_owner_device()->interrupt_in(interrupt_in, buffer.data(), in_packet_size);
}

inline auto HIDBaseDriver::get_buffer() -> std::array<uint8_t, buffer_size>& {
    return buffer;
}

inline auto HIDBaseDriver::get_prev_buffer() -> std::array<uint8_t, buffer_size>& {
    return prev_buffer;
}
} // namespace usb
