#pragma once
#include <deque>

#include "message.hpp"
#include "task.hpp"
#include "usb/classdriver/mouse.hpp"

namespace mouse {
inline auto setup() -> void {
    usb::HIDMouseDriver::default_observer = [](const uint8_t buttons, const int8_t displacement_x, const int8_t displacement_y) -> void {
        auto m       = Message(MessageType::Mouse);
        m.data.mouse = MouseData{buttons, displacement_x, displacement_y};
        task::kernel_task->send_message(m);
    };
}

} // namespace mouse
