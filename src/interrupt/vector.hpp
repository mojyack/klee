#pragma once

namespace interrupt {
enum Vector {
    XHCI = 0x40,
    LAPICTimer,
    AHCI,
    VirtIOGPUControl,
    VirtIOGPUCursor,
};
} // namespace interrupt
