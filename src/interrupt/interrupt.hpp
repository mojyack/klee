#pragma once
#include <array>
#include <cstdint>
#include <deque>

#include "../message.hpp"
#include "fault_handlers.hpp"
#include "type.hpp"

namespace interrupt {
namespace internal {
constexpr auto make_idt_attr(const DescriptorType type, const uint8_t descriptor_privilege_level, const bool present = true, const uint8_t interrupt_stack_table = 0) -> InterruptDescriptorAttribute {
    auto attr                            = InterruptDescriptorAttribute();
    attr.bits.interrupt_stack_table      = interrupt_stack_table;
    attr.bits.type                       = type;
    attr.bits.descriptor_privilege_level = descriptor_privilege_level;
    attr.bits.present                    = present;
    return attr;
}

inline auto set_idt_entry(InterruptDescriptorTable& idt, Vector index, const InterruptDescriptorAttribute attr, const uint64_t offset, const uint16_t segment_selector) -> void {
    auto& desc = idt.data[index];

    desc.attr             = attr;
    desc.offset_low       = offset & 0xFFFFu;
    desc.offset_middle    = (offset >> 16) & 0xFFFFu;
    desc.offset_high      = offset >> 32;
    desc.segment_selector = segment_selector;
}

__attribute__((no_caller_saved_registers)) inline auto notify_end_of_interrupt() -> void {
    lapic::get_registers().end_of_interrupt = 0;
}

__attribute__((interrupt)) static auto int_handler_xhci(InterruptFrame* const frame) -> void {
    process::manager->post_kernel_message(MessageType::XHCIInterrupt);
    notify_end_of_interrupt();
}

__attribute__((interrupt)) static auto int_handler_ahci(InterruptFrame* const frame) -> void {
    process::manager->post_kernel_message(MessageType::AHCIInterrupt);
    notify_end_of_interrupt();
}

__attribute__((interrupt)) static auto int_handler_virtio_gpu_control(InterruptFrame* const frame) -> void {
    process::manager->post_kernel_message(MessageType::VirtIOGPUControl);
    notify_end_of_interrupt();
}

__attribute__((interrupt)) static auto int_handler_virtio_gpu_cursor(InterruptFrame* const frame) -> void {
    process::manager->post_kernel_message(MessageType::VirtIOGPUCursor);
    notify_end_of_interrupt();
}

} // namespace internal

inline auto initialize(InterruptDescriptorTable& idt) -> void {
    const auto cs = read_cs();

#define sie_ist(num, addr, ist) \
    internal::set_idt_entry(idt, static_cast<interrupt::Vector>(num), internal::make_idt_attr(DescriptorType::InterruptGate, 0, true, ist), reinterpret_cast<uint64_t>(addr), cs);

#define sie(num, addr) \
    sie_ist(num, addr, 0)

    sie(0, int_handler_divide_error);
    sie(1, int_handler_debug);
    sie(2, int_handler_nmi);
    sie(3, int_handler_breakpoint);
    sie(4, int_handler_overflow);
    sie(5, int_handler_bound_range_exceeded);
    sie(6, int_handler_invalid_opcode);
    sie(7, int_handler_device_not_available);
    sie(8, int_handler_double_fault);
    sie(9, int_handler_coprocessor_segment_overrun);
    sie(10, int_handler_invalid_tss);
    sie(11, int_handler_segment_not_present);
    sie(12, int_handler_stack_fault);
    sie(13, int_handler_general_protection);
    sie(14, int_handler_page_fault);
    sie(16, int_handler_fpu_floating_point);
    sie(17, int_handler_alignment_check);
    sie(18, int_handler_machine_check);
    sie(19, int_handler_simd_floating_point);
    sie(20, int_handler_virtualization);
    sie(21, int_handler_control_protection);

    sie(Vector::XHCI, internal::int_handler_xhci);
    sie_ist(Vector::LAPICTimer, int_handler_lapic_timer_entry, ist_for_lapic_timer);
    sie(Vector::AHCI, internal::int_handler_ahci);
    sie(Vector::VirtIOGPUControl, internal::int_handler_virtio_gpu_control);
    sie(Vector::VirtIOGPUCursor, internal::int_handler_virtio_gpu_cursor);
    load_idt(sizeof(idt.data) - 1, reinterpret_cast<uintptr_t>(idt.data.data()));

#undef sie
#undef sie_ist
}
} // namespace interrupt
