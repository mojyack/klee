#pragma once
#include "../elf.hpp"
#include "../log.hpp"
#include "manager.hpp"

namespace process {
struct PrepareResult {
    uint64_t entry;
    uint64_t stack;
};

inline auto elf_prepare(const int64_t data, Thread* const thread) -> std::optional<PrepareResult> {
    const auto process = thread->process;

    auto image = std::unique_ptr<memory::SmartFrameID>(std::bit_cast<memory::SmartFrameID*>(data));

    auto elf_info_r = elf::load_elf(*image.get(), process);
    if(!elf_info_r) {
        logger(LogLevel::Error, "failed to load image as elf: %d\n", elf_info_r.as_error().as_int());
        return std::nullopt;
    }
    auto& elf_info = elf_info_r.as_value();

    image.reset();

    {
        auto [lock, allocated_frames] = process->detail->critical_allocated_frames.access();
        allocated_frames = std::move(elf_info.allocated_frames);
    }

    // prepare stack
    constexpr auto stack_frame_addr = 0xFFFF'FFFF'FFFF'F000u;

    auto stack_frame_r = memory::allocate_single();
    if(!stack_frame_r) {
        logger(LogLevel::Error, "failed to allocate frame for application stack: %d\n", stack_frame_r.as_error().as_int());
        return std::nullopt;
    }
    auto& stack_frame = stack_frame_r.as_value();

    {
        auto [lock, pml4] = process->detail->critical_pml4.access();
        paging::map_virtual_to_physical(pml4, stack_frame_addr, std::bit_cast<uint64_t>(stack_frame->get_frame()), paging::Attribute::UserWrite);
    }
    {
        auto [lock, allocated_frames] = process->detail->critical_allocated_frames.access();
        allocated_frames.emplace_back(std::move(stack_frame));
    }

    return PrepareResult{.entry = std::bit_cast<uint64_t>(elf_info.entry), .stack = stack_frame_addr + (0x1000 - 8)};
}

inline auto elf_startup(const uint64_t id, const int64_t data) -> void {
    const auto this_thread = manager->get_this_thread();
    if(const auto result = elf_prepare(data, this_thread)) {
        const auto& prepare = result.value();
        jump_to_app(0, 0,
                    segment::SegmentSelector{.bits = {3, 0, segment::SegmentNumber::UserStack}}.data,
                    prepare.entry,
                    prepare.stack,
                    &this_thread->system_stack_address);
    }
    manager->exit_this_thread();
}
} // namespace process
