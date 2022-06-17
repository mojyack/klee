#pragma once
#include "../../error.hpp"
#include "registers.hpp"

#define CLEAR_STATUS_BIT(bitname)             \
    auto portsc = port_reg_set.portsc.read(); \
    portsc.data[0] &= 0x0E01C3E0u;            \
    portsc.bits.bitname = 1;                  \
    port_reg_set.portsc.write(portsc);

namespace usb::xhci {
class Controller;
class Device;

class Port {
  private:
    uint8_t          port_num;
    PortRegisterSet& port_reg_set;

  public:
    auto get_number() const -> uint8_t {
        return port_num;
    }

    auto is_connected() const -> bool {
        return port_reg_set.portsc.read().bits.current_connect_status;
    }

    auto is_enabled() const -> bool {
        return port_reg_set.portsc.read().bits.port_enabled_disabled;
    }

    auto is_connect_status_changed() const -> bool {
        return port_reg_set.portsc.read().bits.connect_status_change;
    }

    auto is_port_reset_changed() const -> bool {
        return port_reg_set.portsc.read().bits.port_reset_change;
    }

    auto get_speed() const -> int {
        return port_reg_set.portsc.read().bits.port_speed;
    }

    auto reset() -> Error {
        auto portsc = port_reg_set.portsc.read();
        portsc.data[0] &= 0x0e00c3e0u;
        portsc.data[0] |= 0x00020010u; // write 1 to PR and CSC
        port_reg_set.portsc.write(portsc);
        while(port_reg_set.portsc.read().bits.port_reset) {
            //
        }
        return Error::Code::Success;
    }

    auto initialize() -> Device* {
        return nullptr;
    }

    auto clear_connect_status_changed() const -> void {
        CLEAR_STATUS_BIT(connect_status_change);
    }

    auto clear_port_reset_change() const -> void {
        CLEAR_STATUS_BIT(port_reset_change);
    }

    Port(const uint8_t port_num, PortRegisterSet& port_reg_set) : port_num(port_num), port_reg_set(port_reg_set) {}
};
} // namespace usb::xhci

#undef CLEAR_STATUS_BIT
