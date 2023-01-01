#pragma once
#include "smp/ap.hpp"

struct ProcessorResource {
    alignas(0x1000) paging::PML4Table pml4_table;
    segment::GDT gdt;
    SmartFrameID stack;
};
