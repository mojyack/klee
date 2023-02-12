#pragma once
#include <cstdint>

#include "util/critical-queue.hpp"

enum class MessageType {
    XHCIInterrupt,
    AHCIInterrupt,
    VirtIOGPUNewDevice,
    VirtIOGPUControl,
    VirtIOGPUCursor,
    DeviceFinderDone,
};

struct Message {
    MessageType type;

    Message() = default;
    Message(const MessageType type) : type(type) {}
};

inline auto kernel_message_queue = CriticalQueue<Message>();
