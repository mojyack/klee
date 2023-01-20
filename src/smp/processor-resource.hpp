#pragma once
#include "../paging.hpp"
#include "../segment.hpp"

namespace smp {
struct ProcessorResource {
    paging::PML4Table                   pml4_table;
    segment::GDT                        gdt;
    interrupt::InterruptDescriptorTable idt;
    SmartFrameID                        stack;
};
} // namespace smp
