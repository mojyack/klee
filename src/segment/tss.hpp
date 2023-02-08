#pragma once
#include "../memory/allocator.hpp"
#include "segment.hpp"

namespace segment {
struct TaskStateSegment {
    uint32_t reserved1;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved2;
    uint64_t ist[7];
    uint64_t reserved3;
    uint16_t reserved4;
    uint16_t iopb;
} __attribute__((packed));

struct TSSResource {
    std::unique_ptr<TaskStateSegment> tss;
    memory::SmartSingleFrameID                rsp_stack;
    memory::SmartSingleFrameID                rst_stack;
};

inline auto setup_tss(GDT& gdt) -> Result<TSSResource> {
    auto rsp_stack_r = memory::allocate_single();
    if(!rsp_stack_r) {
        return rsp_stack_r.as_error();
    }
    auto& rsp_stack = rsp_stack_r.as_value();

    auto rst_stack_r = memory::allocate_single();
    if(!rst_stack_r) {
        return rst_stack_r.as_error();
    }
    auto& rst_stack = rst_stack_r.as_value();

    auto tss = std::unique_ptr<TaskStateSegment>(new(std::nothrow) TaskStateSegment);
    if(!tss) {
        return Error::Code::NoEnoughMemory;
    }

    tss->rsp0                                    = std::bit_cast<uint64_t>(static_cast<std::byte*>(rsp_stack->get_frame()) + memory::bytes_per_frame);
    tss->ist[interrupt::ist_for_lapic_timer - 1] = std::bit_cast<uint64_t>(static_cast<std::byte*>(rst_stack->get_frame()) + memory::bytes_per_frame);

    const auto tss_addr = reinterpret_cast<uint64_t>(tss.get());
    gdt[TSSLow].set_system_segment(DescriptorType::TSSAvailable, 0, tss_addr & 0xFFFFFFFFu, sizeof(tss) - 1);
    gdt[TSSHigh].data = tss_addr >> 32;
    load_tr(SegmentSelector{.bits = {0, 0, TSSLow}}.data);
    return TSSResource{std::move(tss), std::move(rsp_stack), std::move(rst_stack)};
}
}
