#pragma once
#include <array>

#include "asmcode.h"
#include "x86-descriptor.hpp"

constexpr auto kernel_cs = 1 << 3;
constexpr auto kernel_ss = 2 << 3;
constexpr auto kernel_ds = 0;

union SegmentDescriptor {
    uint64_t data;

    // base and limit are ignored in long mode
    struct {
        uint64_t       limit_low : 16;
        uint64_t       base_low : 16;

        uint64_t       base_middle : 8;

        DescriptorType type : 4;
        uint64_t       system_segment : 1;
        uint64_t       descriptor_privilege_level : 2;
        uint64_t       present : 1;

        uint64_t       limit_high : 4;
        uint64_t       available : 1;
        uint64_t       long_mode : 1;
        uint64_t       default_operation_size : 1;
        uint64_t       granularity : 1;

        uint64_t       base_high : 8;
    } __attribute__((packed)) bits;

    auto set_code_segment(const DescriptorType type, const unsigned int descriptor_privilege_level, const uint32_t base, const uint32_t limit) -> void {
        data = 0;

        bits.base_low    = base & 0xFFFFu;
        bits.base_middle = (base >> 16) & 0xFFu;
        bits.base_high   = (base >> 24) & 0xFFu;

        bits.limit_low  = limit & 0xFFFFu;
        bits.limit_high = (limit >> 16) & 0x0Fu;

        bits.type                       = type;
        bits.system_segment             = 1; // 1: code & data segment
        bits.descriptor_privilege_level = descriptor_privilege_level;
        bits.present                    = 1;
        bits.available                  = 0;
        bits.long_mode                  = 1;
        bits.default_operation_size     = 0; // should be 0 when long_mode == 1
        bits.granularity                = 1;
    }

    auto set_data_segment(const DescriptorType type, const unsigned int descriptor_privilege_level, const uint32_t base, const uint32_t limit) -> void {
        set_code_segment(type, descriptor_privilege_level, base, limit);
        bits.long_mode              = 0;
        bits.default_operation_size = 1; // 32-bit stack segment
    }
} __attribute__((packed));

inline auto setup_segments() -> void {
    static auto gdt = std::array<SegmentDescriptor, 5>();

    gdt[0].data = 0;
    gdt[1].set_code_segment(DescriptorType::ExecuteRead, 0, 0, 0x0FFFFF);
    gdt[2].set_data_segment(DescriptorType::ReadWrite, 0, 0, 0x0FFFFF);
    gdt[3].set_code_segment(DescriptorType::ExecuteRead, 3, 0, 0x0FFFFF);
    gdt[4].set_data_segment(DescriptorType::ReadWrite, 3, 0, 0x0FFFFF);
    load_gdt(sizeof(gdt) - 1, reinterpret_cast<uintptr_t>(gdt.data()));
}
