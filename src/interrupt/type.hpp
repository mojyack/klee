#pragma once
#include <cstdint>

#include "../x86-descriptor.hpp"

namespace interrupt {
union InterruptDescriptorAttribute {
    uint16_t data;
    struct {
        uint16_t interrupt_stack_table : 3;
        uint16_t : 5;

        DescriptorType type : 4;
        uint16_t : 1;
        uint16_t descriptor_privilege_level : 2;
        uint16_t present : 1;
    } __attribute__((packed)) bits;
} __attribute__((packed));

struct InterruptDescriptor {
    uint16_t                     offset_low;
    uint16_t                     segment_selector;
    InterruptDescriptorAttribute attr;
    uint16_t                     offset_middle;
    uint32_t                     offset_high;
    uint32_t                     reserved;
} __attribute__((packed));

struct InterruptFrame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

constexpr auto ist_for_lapic_timer = 1;
} // namespace interrupt
