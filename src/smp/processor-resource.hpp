#pragma once
#include "../memory/frame.hpp"
#include "../paging.hpp"
#include "../segment/segment.hpp"

namespace smp {
struct ProcessorResource {
    segment::GDT                        gdt;
    interrupt::InterruptDescriptorTable idt;
    memory::SmartFrameID                stack;
};
} // namespace smp
