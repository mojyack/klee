#pragma once
#include "../elf.hpp"
#include "../task/manager.hpp"
#include "manager.hpp"

namespace task {
inline auto exit() -> void {
    while(true) {
        __asm__("hlt");
    }
}

inline auto elf_startup(const uint64_t id, const int64_t data) -> void {
    printk("ELF startup %lX %lX\n", data, id);
    const auto image     = std::unique_ptr<SmartFrameID>(reinterpret_cast<SmartFrameID*>(data));
    auto&      this_task = task::task_manager->get_current_task();
    auto&      page_map  = this_task.get_page_map();
    page_map.reset(new PageMap());

    auto elf_info_result = elf::load_elf(*image.get(), page_map->upper_page_map, this_task);
    if(!elf_info_result) {
        printk("elf load error: %d\n", elf_info_result.as_error());
        exit();
    }

    auto& elf_info             = elf_info_result.as_value();
    page_map->allocated_frames = std::move(elf_info.allocated_frames);
    printk("entry %lX\n", elf_info.entry);
    (reinterpret_cast<TaskEntry*>(elf_info.entry))(id, 0);
    exit();
}
} // namespace task
