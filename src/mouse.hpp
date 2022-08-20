#pragma once
#include <deque>

#include "usb/classdriver/mouse.hpp"

namespace mouse {
inline auto setup() -> void {
    usb::HIDMouseDriver::default_observer = [](const uint8_t buttons, const int8_t displacement_x, const int8_t displacement_y) -> void {
    };
}
} // namespace mouse
