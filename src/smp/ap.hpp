#pragma once
#include "../acpi.hpp"
#include "../lapic/registers.hpp"
#include "../log.hpp"
#include "../memory/allocator.hpp"
#include "ipi.hpp"
#include "processor-resource.hpp"

namespace smp {
struct APBootParameter {
    ProcessorResource* processor_resource;
    uint64_t           notify;
};

using APEntry = void(APBootParameter*);

extern "C" std::byte        trampoline;
extern "C" std::byte        trampoline_end;
extern "C" uint32_t         var_cr3;
extern "C" uint64_t         var_kernel_entry;
extern "C" uint64_t         var_kernel_stack;
extern "C" APBootParameter* var_boot_parameter;

inline auto install_trampoline(std::byte* const work, APEntry kernel_entry, const uint64_t kernel_stack, const APBootParameter* const boot_parameter) -> bool {
    static const auto var_cr3_offset            = std::bit_cast<std::byte*>(&var_cr3) - &trampoline;
    static const auto var_kernel_entry_offset   = std::bit_cast<std::byte*>(&var_kernel_entry) - &trampoline;
    static const auto var_kernel_stack_offset   = std::bit_cast<std::byte*>(&var_kernel_stack) - &trampoline;
    static const auto var_boot_parameter_offset = std::bit_cast<std::byte*>(&var_boot_parameter) - &trampoline;

    const auto trampoline_size         = &trampoline_end - &trampoline;
    const auto var_cr3_addr            = std::bit_cast<uint32_t*>(work + var_cr3_offset);
    const auto var_kernel_entry_addr   = std::bit_cast<uint64_t*>(work + var_kernel_entry_offset);
    const auto var_kernel_stack_addr   = std::bit_cast<uint64_t*>(work + var_kernel_stack_offset);
    const auto var_boot_parameter_addr = std::bit_cast<const APBootParameter**>(work + var_boot_parameter_offset);

    const auto pml4 = std::bit_cast<uint64_t>(process::manager->get_this_thread()->process->get_pml4_address());
    if(pml4 >= 0x0000'0001'0000'0000u) {
        logger(LogLevel::Error, "smp: pml4 table is at higher memory, cannot boot ap\n");
        return false;
    }

    memcpy(work, &trampoline, trampoline_size);
    *var_cr3_addr            = pml4;
    *var_kernel_entry_addr   = std::bit_cast<uint64_t>(kernel_entry);
    *var_kernel_stack_addr   = kernel_stack;
    *var_boot_parameter_addr = boot_parameter;
    return true;
}

inline auto send_init_start(const uint8_t target_lapic_id, const uintptr_t start) -> void {
    auto& lapic_registers = lapic::get_registers();

    auto command_low  = InterruptCommandLow{.data = lapic_registers.interrupt_command_0 & 0xFF'F0'00'00u};
    auto command_high = InterruptCommandHigh{.data = lapic_registers.interrupt_command_1 & 0x00'FF'FF'FFu};

    const auto send_command = [&lapic_registers, &command_low, &command_high](const uint64_t delay_us) -> void {
        // writing to the low register(interrupt_command_0) causes the ipi
        lapic_registers.interrupt_command_1 = command_high.data;
        lapic_registers.interrupt_command_0 = command_low.data;

        if(delay_us != 0) {
            acpi::wait_microseconds(delay_us);
        }

        // wait for delivery
        while(InterruptCommandLow{.data = lapic_registers.interrupt_command_0}.bits.delivery_status == DeliveryStatus::SendPending) {
            __asm__("pause");
        }
    };

    // send init
    // - clear apic error
    lapic_registers.error_status = 0;
    // - create command
    command_low.bits.vector                = 0;
    command_low.bits.delivery_mode         = DeliveryMode::Init;
    command_low.bits.destination_mode      = DestinationMode::Physical;
    command_low.bits.level                 = Level::Assert;
    command_low.bits.trigger_mode          = TriggerMode::Level;
    command_low.bits.destination_shorthand = DestinationShorthand::NoShorthand;
    command_high.bits.destination          = target_lapic_id;
    // - apply
    send_command(0);
    // - deassert
    command_low.bits.level = Level::DeAssert;
    send_command(0);
    acpi::wait_miliseconds(10);

    // send startup
    // - clear apic error
    lapic_registers.error_status = 0;
    // - create command
    command_low                            = InterruptCommandLow{.data = lapic_registers.interrupt_command_0 & 0xFF'F0'F8'00u};
    command_low.bits.vector                = start >> 12;
    command_low.bits.delivery_mode         = DeliveryMode::StartUp;
    command_low.bits.destination_shorthand = DestinationShorthand::NoShorthand;
    // - apply
    send_command(200);
}

inline auto start_ap(const memory::FrameID page, const uint8_t target_lapic_id, APEntry kernel_entry, const uint64_t kernel_stack, const APBootParameter* const boot_parameter) -> bool {
    if(std::bit_cast<uintptr_t>(page.get_frame()) > 0xFF000lu) {
        logger(LogLevel::Error, "smp: allocated page is not in a real-mode address space\n");
        return false;
    }

    const auto trampoline_size = &trampoline_end - &trampoline;
    if(trampoline_size > memory::bytes_per_frame) {
        logger(LogLevel::Error, "smp: trampoline code is too big(%lu > %lu)\n", trampoline_size, memory::bytes_per_frame);
        return false;
    }

    const auto work = page.get_frame();
    logger(LogLevel::Debug, "smp: using frame %x as a trampoline code, code size is %lu bytes\n", std::bit_cast<size_t>(work) >> 12, trampoline_size);

    if(!install_trampoline(std::bit_cast<std::byte*>(work), kernel_entry, kernel_stack, boot_parameter)) {
        return false;
    }
    send_init_start(target_lapic_id, std::bit_cast<uintptr_t>(work));

    return true;
}
} // namespace smp
