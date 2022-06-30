#pragma once
#include <deque>

#include "message.hpp"
#include "usb/classdriver/mouse.hpp"

namespace mouse {
inline auto setup(std::deque<Message>& main_queue) -> void {
    usb::HIDMouseDriver::default_observer = [&main_queue](const uint8_t buttons, const int8_t displacement_x, const int8_t displacement_y) -> void {
        auto m       = Message(MessageType::Mouse);
        m.data.mouse = MouseData{buttons, displacement_x, displacement_y};
        main_queue.push_back(m);
    };
}

} // namespace mouse
