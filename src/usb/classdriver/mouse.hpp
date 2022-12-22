#pragma once
#include <functional>

#include "hid-decl.hpp"

namespace usb {
class HIDMouseDriver : public HIDBaseDriver {
  private:
    using ObserverType = void(uint8_t buttons, int8_t displacement_x, int8_t displacement_y);

    std::array<std::function<ObserverType>, 4> observers;

    auto notify_mousemove(const uint8_t buttons, const int8_t displacement_x, const int8_t displacement_y) -> void {
        for(const auto& o : observers) {
            if(!o) {
                break;
            }
            o(buttons, displacement_x, displacement_y);
        }
    }

  public:
    static inline std::function<ObserverType> default_observer;

    auto on_data_received() -> Error override {
        const auto buttons        = get_buffer()[0];
        const auto displacement_x = get_buffer()[1];
        const auto displacement_y = get_buffer()[2];
        notify_mousemove(buttons, displacement_x, displacement_y);
        return Error::Code::Success;
    }

    auto subscribe_mousemove(const std::function<ObserverType> observer) -> void {
        for(auto& o : observers) {
            if(o) {
                continue;
            }
            o = observer;
            return;
        }
    }

    HIDMouseDriver(Device* const device, const int interface_index) : HIDBaseDriver(device, interface_index, 3) {}
};
} // namespace usb
