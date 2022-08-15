#pragma once
#include "../elf.hpp"
#include "../log.hpp"
#include "../task/manager.hpp"
#include "manager.hpp"

namespace task {
inline auto elf_startup(const uint64_t id, const int64_t data) -> void {
    auto& task = task::task_manager->get_current_task();

    // load image
    const auto image    = std::unique_ptr<SmartFrameID>(reinterpret_cast<SmartFrameID*>(data));
    auto&      page_map = task.get_page_map();
    page_map.reset(new PageMap());

    auto elf_info_result = elf::load_elf(*image.get(), page_map->upper_page_map, task);
    if(!elf_info_result) {
        logger(LogLevel::Error, "failed to load image as elf: %d\n", elf_info_result.as_error().as_int());
        task.exit();
    }
    auto& elf_info             = elf_info_result.as_value();
    page_map->allocated_frames = std::move(elf_info.allocated_frames);

    // prepare stack
    constexpr auto max_address      = 0xFFFF'807F'FFFF'FFFFu; // this is max available address, because pml4 index is fixed to 256
    constexpr auto stack_frame_addr = max_address - 0x0FFFu;

    auto stack_frame = allocator->allocate(1);
    if(!stack_frame) {
        logger(LogLevel::Error, "failed to allocate frame for application stack");
        task.exit();
    }
    paging::map_virtual_to_physical(&page_map->upper_page_map, stack_frame_addr, reinterpret_cast<uintptr_t>(stack_frame.as_value().get_frame()), paging::Attribute::UserWrite);
    page_map->allocated_frames.emplace_back(stack_frame.as_value(), 1);
    printk("map %lX to %lX\n", stack_frame_addr, reinterpret_cast<uintptr_t>(stack_frame.as_value().get_frame()));
    printk("stack begin at %lX\n", stack_frame_addr + (0x1000 - 8));
    printk("entry at %lX\n", elf_info.entry);

    // [[ noreturn ]]
    jump_to_app(id, 0, segment::SegmentSelector{.bits = {3, 0, segment::SegmentNumber::UserCode}}.data, segment::SegmentSelector{.bits = {3, 0, segment::SegmentNumber::UserStack}}.data, reinterpret_cast<uint64_t>(elf_info.entry), stack_frame_addr + (0x1000 - 8));

    //(reinterpret_cast<TaskEntry*>(elf_info.entry))(id, reinterpret_cast<int64_t>(static_cast<int (*)(const char*, ...)>(printk)));
    task.exit();
}
} // namespace task
