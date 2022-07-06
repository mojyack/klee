#pragma once
namespace interrupt {
class InterruptVector {
  public:
    enum Number {
        XHCI = 0x40,
        LAPICTimer,
        AHCI,
        VirtIOGPUControl,
        VirtIOGPUCursor,
    };
};
} // namespace interrupt
