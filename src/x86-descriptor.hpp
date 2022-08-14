#pragma once

enum class DescriptorType {
    // system segment & gate descriptor types
    Upper8Bytes   = 0b0000,
    LDT           = 0b0010,
    TSSAvailable  = 0b1001,
    TSSBusy       = 0b1011,
    CallGate      = 0b1100,
    InterruptGate = 0b1110,
    TrapGate      = 0b1111,

    // code & data segment types
    ReadWrite   = 0b0010,
    ExecuteRead = 0b1010,
};
