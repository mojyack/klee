#pragma once
#include <cstdint>

namespace smp {
enum class DeliveryMode : uint64_t {
    Fixed          = 0b000,
    LowestPriority = 0b001,
    SMI            = 0b010,
    NMI            = 0b100,
    Init           = 0b101,
    StartUp        = 0b110,
    // Reserved = 0b011,
    // Reserved = 0b111,
};

enum class DestinationMode : uint64_t {
    Physical = 0b0,
    Logical  = 0b1,
};

enum class DeliveryStatus : uint64_t {
    Idle        = 0b0,
    SendPending = 0b1,
};

enum class Level : uint64_t {
    DeAssert = 0b0,
    Assert   = 0b1,
};

enum class TriggerMode : uint64_t {
    Edge  = 0b0,
    Level = 0b1,
};

enum class DestinationShorthand : uint64_t {
    NoShorthand   = 0b00,
    Self          = 0b01,
    All           = 0b10,
    AllExceptSelf = 0b11,
};

union InterruptCommandLow {
    uint32_t data;
    struct {
        uint64_t             vector : 8;
        DeliveryMode         delivery_mode : 3;
        DestinationMode      destination_mode : 1;
        DeliveryStatus       delivery_status : 1; // ro
        uint64_t             reserved1 : 1;
        Level                level : 1;
        TriggerMode          trigger_mode : 1;
        uint64_t             reserved2 : 2;
        DestinationShorthand destination_shorthand : 2;
        uint64_t             reserved3 : 12;
    } __attribute__((packed)) bits;
};

union InterruptCommandHigh {
    uint32_t data;
    struct {
        uint64_t reserved1 : 24;
        uint64_t destination : 8;
    } __attribute__((packed)) bits;
};

static_assert(sizeof(InterruptCommandLow) == 4);
static_assert(sizeof(InterruptCommandHigh) == 4);
} // namespace smp
