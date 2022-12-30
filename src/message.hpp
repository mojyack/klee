#pragma once
#include <cstdint>

#include "util/critical-queue.hpp"

enum class MessageType {
    XHCIInterrupt,
    AHCIInterrupt,
    Timer, // TimerData
    VirtIOGPUNewDevice,
    VirtIOGPUControl,
    VirtIOGPUCursor,
    DeviceFinderDone,
};

struct TimerData {
    int value;
};

struct Message {
    MessageType type;
    union {
        TimerData timer;
    } data;

    Message() = default;
    Message(const MessageType type) : type(type) {}
};

inline auto kernel_message_queue = CriticalQueue<Message>();
