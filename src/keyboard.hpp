#pragma once
#include <deque>

#include "usb/classdriver/keyboard.hpp"
#include "devfs/keyboard.hpp"

namespace keyboard {
namespace internal {
constexpr auto ascii_table = std::array<char, 256>{
    0, 0, 0, 0, 'a', 'b', 'c', 'd',             // 0
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',     // 8
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't',     // 16
    'u', 'v', 'w', 'x', 'y', 'z', '1', '2',     // 24
    '3', '4', '5', '6', '7', '8', '9', '0',     // 32
    '\n', '\b', 0x08, '\t', ' ', '-', '=', '[', // 40
    ']', '\\', '#', ';', '\'', '`', ',', '.',   // 48
    '/', 0, 0, 0, 0, 0, 0, 0,                   // 56
    0, 0, 0, 0, 0, 0, 0, 0,                     // 64
    0, 0, 0, 0, 0, 0, 0, 0,                     // 72
    0, 0, 0, 0, '/', '*', '-', '+',             // 80
    '\n', '1', '2', '3', '4', '5', '6', '7',    // 88
    '8', '9', '0', '.', '\\', 0, 0, '=',        // 96
};

constexpr auto ascii_table_shift = std::array<char, 256>{
    0, 0, 0, 0, 'A', 'B', 'C', 'D',             // 0
    'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',     // 8
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',     // 16
    'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@',     // 24
    '#', '$', '%', '^', '&', '*', '(', ')',     // 32
    '\n', '\b', 0x08, '\t', ' ', '_', '+', '{', // 40
    '}', '|', '~', ':', '"', '~', '<', '>',     // 48
    '?', 0, 0, 0, 0, 0, 0, 0,                   // 56
    0, 0, 0, 0, 0, 0, 0, 0,                     // 64
    0, 0, 0, 0, 0, 0, 0, 0,                     // 72
    0, 0, 0, 0, '/', '*', '-', '+',             // 80
    '\n', '1', '2', '3', '4', '5', '6', '7',    // 88
    '8', '9', '0', '.', '\\', 0, 0, '=',        // 96
};
} // namespace internal

enum Modifiers {
    LControl = 0b00000001u,
    LShift   = 0b00000010u,
    LAlt     = 0b00000100u,
    LGUI     = 0b00001000u,
    RControl = 0b00010000u,
    RShift   = 0b00100000u,
    RAlt     = 0b01000000u,
    RGUI     = 0b10000000u,
};

inline auto setup(devfs::USBKeyboard& devfs_usb_keyboard) -> void {
    usb::HIDKeyboardDriver::default_observer = [&devfs_usb_keyboard](const uint8_t modifier, const uint8_t keycode) -> void {
        const bool shift = (modifier & (Modifiers::LShift | Modifiers::RShift)) != 0;

        devfs_usb_keyboard.push_packet(fs::dev::KeyboardPacket{keycode, modifier, !shift ? internal::ascii_table[keycode] : internal::ascii_table_shift[keycode]});
    };
}
} // namespace keyboard
