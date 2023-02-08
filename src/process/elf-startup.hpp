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
    const auto lock    = AutoLock(process->page_map_mutex);

    auto image = std::unique_ptr<memory::SmartFrameID>(std::bit_cast<memory::SmartFrameID*>(data));
    process->page_map.reset(new PageMap());

    auto elf_info_r = elf::load_elf(*image.get(), process->page_map->upper_page_map, process, lock);
    if(!elf_info_r) {
        logger(LogLevel::Error, "failed to load image as elf: %d\n", elf_info_r.as_error().as_int());
        return std::nullopt;
    }
    auto& elf_info = elf_info_r.as_value();

    image.reset();
    process->page_map->allocated_frames = std::move(elf_info.allocated_frames);

    // prepare stack
    constexpr auto max_address      = 0xFFFF'807F'FFFF'FFFFu; // this is max available address, because pml4 index is fixed to 256
    constexpr auto stack_frame_addr = max_address - 0x0FFFu;

    auto stack_frame_r = memory::allocate_single();
    if(!stack_frame_r) {
        logger(LogLevel::Error, "failed to allocate frame for application stack: %d\n", stack_frame_r.as_error().as_int());
        return std::nullopt;
    }
    auto& stack_frame = stack_frame_r.as_value();

    paging::map_virtual_to_physical(&process->page_map->upper_page_map, stack_frame_addr, reinterpret_cast<uintptr_t>(stack_frame->get_frame()), paging::Attribute::UserWrite);
    process->page_map->allocated_frames.emplace_back(std::move(stack_frame));

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
