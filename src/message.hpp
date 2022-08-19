#pragma once
#include <cstdint>

enum class MessageType {
    XHCIInterrupt,
    AHCIInterrupt,
    Timer,    // TimerData
    VirtIOGPUNewDevice,
    VirtIOGPUControl,
    VirtIOGPUCursor,
};

struct TimerData {
    int value;
};

struct Message {
    MessageType type;
    union {
        TimerData    timer;
    } data;

    Message() = default;
    Message(const MessageType type) : type(type) {}
};
