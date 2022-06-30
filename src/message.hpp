#pragma once
#include <cstdint>

enum class MessageType {
    XHCIInterrupt,
    LAPICTimer,
    Keyboard, // KeyboardData
    VirtIOGPUControl,
    VirtIOGPUCursor,
};

struct KeyboardData {
    uint8_t keycode;
    uint8_t modifier;
    char    ascii;
};

struct Message {
    MessageType type;
    union {
        KeyboardData keyboard;
    } data;

    Message() = default;
    Message(const MessageType type) : type(type) {}
};
