#pragma once
#include "base.hpp"

namespace usb {
class HIDBaseDriver : public ClassDriver {
  private:
    EndpointID interrupt_in;
    EndpointID interrupt_out;
    int        interface_index;
    int        in_packet_size;
    int        initialize_phase = 0;

    constexpr static auto            buffer_size = 1024;
    std::array<uint8_t, buffer_size> buffer;
    std::array<uint8_t, buffer_size> prev_buffer;

  public:
    virtual auto on_data_received() -> Error = 0;

    auto initialize() -> Error override;
    auto set_endpoint(const EndpointConfig& config) -> Error override;
    auto on_endpoint_configured() -> Error override;
    auto on_control_completed(const EndpointID id, const SetupData& setup_data, const void* const buf, const int len) -> Error override;
    auto on_interrupt_completed(const EndpointID id, const void* const buf, const int len) -> Error override;
    auto get_buffer() -> std::array<uint8_t, buffer_size>&;
    auto get_prev_buffer() -> std::array<uint8_t, buffer_size>&;

    HIDBaseDriver(Device* const device, const int interface_index, const int in_packet_size) : ClassDriver(device),
                                                                                               interface_index(interface_index),
                                                                                               in_packet_size(in_packet_size) {}

    virtual ~HIDBaseDriver() {}
};
} // namespace usb
