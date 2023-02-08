#pragma once
#include "../memory/frame.hpp"
#include "../paging.hpp"
#include "../segment/segment.hpp"

namespace smp {
struct ProcessorResource {
    paging::PML4Table                   pml4_table;
    segment::GDT                        gdt;
    interrupt::InterruptDescriptorTable idt;
    memory::SmartFrameID                stack;
};
} // namespace smp
