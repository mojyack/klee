#pragma once
#include <array>
#include <deque>
#include <vector>

#include "asmcode.h"
#include "error.hpp"
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

    auto get_id() const -> uint64_t {
        return id;
    }

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
    std::deque<Task*>                  running;
    uint64_t                           last_id            = 0;

  public:
    auto new_task() -> Task& {
        last_id += 1;
        return *tasks.emplace_back(new Task(last_id));
    }

    auto switch_task(const bool sleep_current = false) -> void {
        const auto current_task = running.front();
        running.pop_front();
        if(!sleep_current) {
            running.push_back(current_task);
        }
        const auto next_task = running.front();

        switch_context(&next_task->get_context(), &current_task->get_context());
    }

    auto sleep(Task* const task) -> void {
        const auto it = std::find(running.begin(), running.end(), task);

        if(it == running.begin()) {
            switch_task(true);
            return;
        }

        if(it == running.end()) {
            return;
        }

        running.erase(it);
    }

    auto sleep(const uint64_t id) -> Error {
        const auto it = std::find_if(tasks.begin(), tasks.end(), [id](const auto& t) { return t->get_id() == id; });
        if(it == tasks.end()) {
            return Error::Code::NoSuchTask;
        }

        sleep(it->get());
        return Error::Code::Success;
    }

    auto wakeup(Task* const task) -> void {
        const auto it = std::find(running.begin(), running.end(), task);
        if(it == running.end()) {
            running.push_back(task);
        }
    }

    auto wakeup(const uint64_t id) -> Error {
        const auto it = std::find_if(tasks.begin(), tasks.end(), [id](const auto& t) { return t->get_id() == id; });
        if(it == tasks.end()) {
            return Error::Code::NoSuchTask;
        }

        wakeup(it->get());
        return Error::Code::Success;
    }

    TaskManager() {
        running.push_back(&new_task());
    }
};

inline auto task_manager = (TaskManager*)(nullptr);
} // namespace task
