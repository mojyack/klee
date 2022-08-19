#pragma once
#include <cstdint>

enum class MessageType {
    XHCIInterrupt,
    AHCIInterrupt,
    Timer,    // TimerData
    Keyboard, // KeyboardData
    Mouse,    // MouseData
    RefreshScreen,
    RefreshScreenDone,
    ScreenResized,
    VirtIOGPUNewDevice,
    VirtIOGPUControl,
    VirtIOGPUCursor,
};

struct TimerData {
    int value;
};

struct KeyboardData {
    uint8_t keycode;
    uint8_t modifier;
    char    ascii;
};

struct MouseData {
    uint8_t buttons;
    int8_t  displacement_x;
    int8_t  displacement_y;
};

struct Message {
    MessageType type;
    union {
        TimerData    timer;
        KeyboardData keyboard;
        MouseData    mouse;
    } data;

    Message() = default;
    Message(const MessageType type) : type(type) {}
};
