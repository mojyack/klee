#pragma once
#include <array>

#include "asmcode.h"
#include "macro.hpp"
#include "memory-manager.hpp"
#include "x86-descriptor.hpp"

namespace segment {
union SegmentDescriptor {
    uint64_t data;

    // base and limit are ignored in long mode
    struct {
        uint64_t limit_low : 16;
        uint64_t base_low : 16;

        uint64_t base_middle : 8;

        DescriptorType type : 4;
        uint64_t       system_segment : 1;
        uint64_t       descriptor_privilege_level : 2;
        uint64_t       present : 1;

        uint64_t limit_high : 4;
        uint64_t available : 1;
        uint64_t long_mode : 1;
        uint64_t default_operation_size : 1;
        uint64_t granularity : 1;

        uint64_t base_high : 8;
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

    auto set_system_segment(const DescriptorType type, const unsigned int descriptor_privilege_level, const uint32_t base, const uint32_t limit) -> void {
        set_code_segment(type, descriptor_privilege_level, base, limit);
        bits.system_segment = 0;
        bits.long_mode      = 0;
    }
} __attribute__((packed));

union SegmentSelector {
    uint16_t data;
    struct {
        uint16_t rpl : 2;    // requested privilege level
        uint16_t ti : 1;     // if 0 use gdt, else use ldt
        uint16_t index : 13; // index in descriptor table
    } bits __attribute__((packed));
} __attribute__((packed));

struct TaskStateSegment {
    uint32_t reserved1;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved2;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved3;
    uint16_t reserved4;
    uint16_t iopb;
} __attribute__((packed));

static auto gdt = std::array<SegmentDescriptor, 7>();

enum SegmentNumber : uint16_t {
    Null        = 0,
    KernelCode  = 1,
    KernelStack = 2,
    UserCode    = 4,
    UserStack   = 3,
    TSSLow      = 5,
    TSSHigh     = 6,
};

constexpr auto kernel_cs = segment::SegmentSelector{.bits = {.index = segment::SegmentNumber::KernelCode}};
constexpr auto kernel_ss = segment::SegmentSelector{.bits = {.index = segment::SegmentNumber::KernelStack}};
constexpr auto kernel_ds = segment::SegmentSelector{.bits = {.index = segment::SegmentNumber::Null}};

inline auto setup_segments() -> void {
    gdt[SegmentNumber::Null].data = 0;
    gdt[SegmentNumber::KernelCode].set_code_segment(DescriptorType::ExecuteRead, 0, 0, 0x0FFFFF);
    gdt[SegmentNumber::KernelStack].set_data_segment(DescriptorType::ReadWrite, 0, 0, 0x0FFFFF);
    gdt[SegmentNumber::UserCode].set_code_segment(DescriptorType::ExecuteRead, 3, 0, 0x0FFFFF);
    gdt[SegmentNumber::UserStack].set_data_segment(DescriptorType::ReadWrite, 3, 0, 0x0FFFFF);
    load_gdt(sizeof(gdt) - 1, reinterpret_cast<uintptr_t>(gdt.data()));

    set_dsall(kernel_ds.data);
    set_csss(kernel_cs.data, kernel_ss.data);
}

inline auto setup_tss() -> Error {
    static auto tss_stack = SmartFrameID();
    static auto tss       = TaskStateSegment();

    value_or(stack, allocator->allocate(1));
    tss_stack = SmartFrameID(stack, 1);
    tss.rsp0  = reinterpret_cast<uint64_t>(static_cast<uint8_t*>(tss_stack->get_frame()) + bytes_per_frame);

    const auto tss_addr = reinterpret_cast<uint64_t>(&tss);
    gdt[TSSLow].set_system_segment(DescriptorType::TSSAvailable, 0, tss_addr & 0xFFFFFFFFu, sizeof(tss) - 1);
    gdt[TSSHigh].data = tss_addr >> 32;
    load_tr(SegmentSelector{.bits = {0, 0, 5}}.data);
    return Error();
}
} // namespace segment

