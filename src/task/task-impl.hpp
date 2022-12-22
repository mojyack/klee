#pragma once
#include "manager.hpp"

namespace task {
inline auto Task::get_id() const -> uint64_t {
    return id;
}

inline auto Task::init_context(TaskEntry* const func, const int64_t data) -> Error {
    entry = func;

    const auto stack_size = default_stack_bytes / sizeof(stack[0]);
    stack.resize(stack_size);
    const auto stack_end = reinterpret_cast<uint64_t>(&stack[stack_size]);

    memset(&context, 0, sizeof(context));
    context.rip = reinterpret_cast<uint64_t>(func);
    context.rdi = id;
    context.rsi = data;

    context.cr3    = get_cr3();
    context.rflags = 0x202;
    context.cs     = segment::kernel_cs.data;
    context.ss     = segment::kernel_ss.data;
    context.rsp    = (stack_end & ~0x0Flu) - 8;

    // mask all exceptions of MXCSR
    *reinterpret_cast<uint32_t*>(&context.fxsave_area[24]) = 0x1f80;

    return Success();
}

inline auto Task::send_message(Message message) -> void {
    messages.push_back(std::move(message));
    wakeup();
}

inline auto Task::send_message_may_fail(Message message) -> void {
    messages.push_back(std::move(message));
    wakeup_may_fail();
}

inline auto Task::receive_message() -> std::optional<Message> {
    if(messages.empty()) {
        return std::nullopt;
    }

    auto m = messages.front();
    messages.pop_front();
    return m;
}

inline auto Task::exit() -> void {
    manager->exit_task(this);
}

inline auto Task::sleep() -> Task& {
    manager->sleep(this);
    return *this;
}

inline auto Task::wakeup(const int nice) -> Task& {
    manager->wakeup(this, nice);
    return *this;
}

inline auto Task::wakeup_may_fail() -> void {
    manager->wakeup_may_fail(this);
}

inline auto Task::wait_event(const uint64_t event_id) -> void {
    manager->wait_event(event_id, this);
}

inline auto Task::wait_events(std::vector<uint64_t> event_ids) -> void {
    manager->wait_events(std::move(event_ids), this);
}

inline auto Task::get_system_stack_pointer() -> uint64_t& {
    return system_stack_pointer;
}

inline auto Task::get_context() -> TaskContext& {
    return context;
}

inline auto Task::get_page_map() -> std::unique_ptr<PageMap>& {
    return page_map;
}

inline auto Task::apply_page_map() -> void {
    auto& pml4e = paging::pml4_table[0b100000000];
    if(page_map) {
        pml4e.data              = reinterpret_cast<uint64_t>(page_map->upper_page_map.data.data());
        pml4e.directory.present = 1;
        pml4e.directory.write   = 1;
        pml4e.directory.user    = 1;
    } else {
        pml4e.data = 0;
    }
}
} // namespace task
