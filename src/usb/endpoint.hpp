#pragma once

namespace usb {
enum class EndpointType {
    Control,
    Isochronous,
    Bulk,
    Interrupt
};

class EndpointID {
  private:
    int address;

  public:
    auto get_address() const -> int {
        return address;
    }

    auto get_number() const -> int {
        return address >> 1;
    }

    auto is_in() const -> bool {
        return address & 1;
    }

    constexpr EndpointID() : address(0) {}
    constexpr EndpointID(const int endpoint_number, const bool in) : address(endpoint_number << 1 | in) {}
    explicit constexpr EndpointID(const int address) : address(address) {}
};

struct EndpointConfig {
    EndpointID   id;
    EndpointType type;
    int          max_packet_size;
    int          interval;
};

constexpr auto default_control_pipe_id = EndpointID(0, true);
} // namespace usb
