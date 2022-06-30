#pragma once
#include <array>
#include <cstdint>
#include <deque>

#include "asmcode.h"
#include "message.hpp"
#include "x86-descriptor.hpp"

namespace interrupt {
class InterruptVector {
  public:
    enum Number {
        XHCI = 0x40,
        LAPICTimer,
        VirtIOGPUControl,
        VirtIOGPUCursor,
    };
};

namespace internal {
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

inline auto idt = std::array<InterruptDescriptor, 256>();

constexpr auto make_idt_attr(const DescriptorType type, const uint8_t descriptor_privilege_level, const bool present = true, const uint8_t interrupt_stack_table = 0) -> InterruptDescriptorAttribute {
    auto attr                            = InterruptDescriptorAttribute();
    attr.bits.interrupt_stack_table      = interrupt_stack_table;
    attr.bits.type                       = type;
    attr.bits.descriptor_privilege_level = descriptor_privilege_level;
    attr.bits.present                    = present;
    return attr;
}

inline auto set_idt_entry(InterruptVector::Number index, const InterruptDescriptorAttribute attr, const uint64_t offset, const uint16_t segment_selector) -> void {
    auto& desc = idt[index];

    desc.attr             = attr;
    desc.offset_low       = offset & 0xFFFFu;
    desc.offset_middle    = (offset >> 16) & 0xFFFFu;
    desc.offset_high      = offset >> 32;
    desc.segment_selector = segment_selector;
}

__attribute__((no_caller_saved_registers)) inline auto notify_end_of_interrupt() -> void {
    volatile auto end_of_interrupt = reinterpret_cast<uint32_t*>(0xFEE000B0);

    *end_of_interrupt = 0;
}

inline auto main_queue = (std::deque<Message>*)(nullptr);

__attribute__((interrupt)) static auto int_handler_xhci(InterruptFrame* const frame) -> void {
    main_queue->push_back(MessageType::XHCIInterrupt);
    notify_end_of_interrupt();
}

__attribute__((interrupt)) static auto int_handler_lapic_timer(InterruptFrame* const frame) -> void {
    main_queue->push_back(MessageType::LAPICTimer);
    notify_end_of_interrupt();
}

__attribute__((interrupt)) static auto int_handler_virtio_gpu_control(InterruptFrame* const frame) -> void {
    main_queue->push_back(MessageType::VirtIOGPUControl);
    notify_end_of_interrupt();
}

__attribute__((interrupt)) static auto int_handler_virtio_gpu_cursor(InterruptFrame* const frame) -> void {
    main_queue->push_back(MessageType::VirtIOGPUCursor);
    notify_end_of_interrupt();
}

} // namespace internal
inline auto initialize(std::deque<Message>& main_queue) -> void {
    internal::main_queue = &main_queue;

    const auto cs = read_cs();
    set_idt_entry(InterruptVector::XHCI, internal::make_idt_attr(DescriptorType::InterruptGate, 0), reinterpret_cast<uint64_t>(internal::int_handler_xhci), cs);
    set_idt_entry(InterruptVector::LAPICTimer, internal::make_idt_attr(DescriptorType::InterruptGate, 0), reinterpret_cast<uint64_t>(internal::int_handler_lapic_timer), cs);
    set_idt_entry(InterruptVector::VirtIOGPUControl, internal::make_idt_attr(DescriptorType::InterruptGate, 0), reinterpret_cast<uint64_t>(internal::int_handler_virtio_gpu_control), cs);
    set_idt_entry(InterruptVector::VirtIOGPUCursor, internal::make_idt_attr(DescriptorType::InterruptGate, 0), reinterpret_cast<uint64_t>(internal::int_handler_virtio_gpu_cursor), cs);
    load_idt(sizeof(internal::idt) - 1, reinterpret_cast<uintptr_t>(&internal::idt[0]));
}
} // namespace interrupt
