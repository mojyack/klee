#pragma once
#include <array>
#include <vector>

#include "asmcode.h"
#include "segment.hpp"

namespace task {
struct TaskContext {
    uint64_t                 cr3, rip, rflags, reserved1;            // offset 0x00
    uint64_t                 cs, ss, fs, gs;                         // offset 0x20
    uint64_t                 rax, rbx, rcx, rdx, rdi, rsi, rsp, rbp; // offset 0x40
    uint64_t                 r8, r9, r10, r11, r12, r13, r14, r15;   // offset 0x80
    std::array<uint8_t, 512> fxsave_area;                            // offset 0xc0
} __attribute__((packed));

using TaskEntry = void(uint64_t, int64_t);

class Task {
  private:
    uint64_t              id;
    std::vector<uint64_t> stack;
    alignas(16) TaskContext context;

  public:
    static constexpr auto default_stack_bytes = size_t(4096);

    auto init_context(TaskEntry* func, const int64_t data) -> Task& {
        const auto stack_size = default_stack_bytes / sizeof(stack[0]);
        stack.resize(stack_size);
        const auto stack_end = reinterpret_cast<uint64_t>(&stack[stack_size]);

        memset(&context, 0, sizeof(context));
        context.rip = reinterpret_cast<uint64_t>(func);
        context.rdi = id;
        context.rsi = data;

        context.cr3    = get_cr3();
        context.rflags = 0x202;
        context.cs     = kernel_cs;
        context.ss     = kernel_ss;
        context.rsp    = (stack_end & ~0x0Flu) - 8;

        // mask all exceptions of MXCSR
        *reinterpret_cast<uint32_t*>(&context.fxsave_area[24]) = 0x1f80;

        return *this;
    }

    auto get_context() -> TaskContext& {
        return context;
    }

    Task(uint64_t id) : id(id) {}
};

class TaskManager {
  private:
    std::vector<std::unique_ptr<Task>> tasks;
    uint64_t                           last_id            = 0;
    size_t                             current_task_index = 0;

  public:
    auto new_task() -> Task& {
        last_id += 1;
        return *tasks.emplace_back(new Task(last_id));
    }

    auto switch_task() -> void {
        const auto next = (current_task_index + 1) % tasks.size();

        auto& current_task = *tasks[current_task_index];
        auto& next_task    = *tasks[next];
        current_task_index = next;

        switch_context(&next_task.get_context(), &current_task.get_context());
    }

    TaskManager() {
        new_task();
    }
};

inline auto task_manager = (TaskManager*)(nullptr);
} // namespace task
