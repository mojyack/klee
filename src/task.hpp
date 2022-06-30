#pragma once
#include <array>
#include <deque>
#include <optional>
#include <vector>

#include "asmcode.h"
#include "error.hpp"
#include "message.hpp"
#include "segment.hpp"
#include "util.hpp"
#include "print.hpp"

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
    std::deque<Message>   messages;
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

    auto send_message(Message message) -> void {
        messages.push_back(std::move(message));
        wakeup();
    }

    auto receive_message() -> std::optional<Message> {
        if(messages.empty()) {
            return std::nullopt;
        }

        auto m = messages.front();
        messages.pop_front();
        return m;
    }

    auto sleep() -> Task&;
    auto wakeup() -> Task&;

    Task(const uint64_t id) : id(id) {}
};

class TaskManager {
  private:
    constexpr static auto max_nice = 3;

    struct ManagedTask {
        Task     task;
        uint32_t nice    = max_nice / 2;
        bool     running = false;

        ManagedTask(const uint64_t id) : task(id) {}
    };

    std::vector<std::unique_ptr<ManagedTask>>          tasks;
    std::array<std::deque<ManagedTask*>, max_nice + 1> running;
    uint64_t                                           last_id      = 0;
    int                                                current_nice = 0;
    bool                                               nice_changed = false;

    template <class T, class U>
    void erase(T& c, const U& value) {
        const auto it = std::remove(c.begin(), c.end(), value);
        c.erase(it, c.end());
    }

    static auto idle_main(const uint64_t id, const int64_t data) -> void {
        while(true) {
            __asm__("hlt");
        }
    }

    auto change_nice_running(ManagedTask* const task, const int nice) -> void {
        if(nice < 0 || nice == task->nice) {
            return;
        }

        if(task != running[current_nice].front()) {
            // change level of other task
            erase(running[task->nice], task);
            running[nice].push_back(task);
            task->nice = nice;
            if(nice < current_nice) {
                nice_changed = true;
            }
            return;
        }

        // change level myself
        running[current_nice].pop_front();
        running[nice].push_front(task);
        task->nice   = nice;
        current_nice = nice;
        if(nice > current_nice) {
            nice_changed = true;
        }
    }

    auto new_managed_task() -> ManagedTask& {
        last_id += 1;
        return *tasks.emplace_back(new ManagedTask(last_id));
    }

    auto sleep(ManagedTask* const task) -> void {
        if(!task->running) {
            return;
        }
        task->running = false;

        if(task == running[current_nice].front()) {
            switch_task(true);
            return;
        }

        erase(running[task->nice], task);
    }

    auto wakeup(ManagedTask* const task, int nice) -> void {
        if(task->running) {
            change_nice_running(task, nice);
            return;
        }

        nice          = nice >= 0 ? nice : task->nice;
        task->nice    = nice;
        task->running = true;

        running[nice].push_back(task);
        if(nice < current_nice) {
            nice_changed = true;
        }
    }

  public:
    auto new_task() -> Task& {
        return new_managed_task().task;
    }

    auto switch_task(const bool sleep_current = false) -> void {
        auto&      nice_queue   = running[current_nice];
        const auto current_task = nice_queue.front();
        nice_queue.pop_front();

        if(!sleep_current) {
            nice_queue.push_back(current_task);
        }

        if(nice_queue.empty()) {
            nice_changed = true;
        }

        if(nice_changed) {
            nice_changed = false;
            for(auto n = 0; n <= max_nice; n += 1) {
                if(running[n].empty()) {
                    continue;
                }
                current_nice = n;
                break;
            }
        }

        const auto next_task = running[current_nice].front();

        switch_context(&next_task->task.get_context(), &current_task->task.get_context());
    }

    auto sleep(Task* const task) -> void {
        sleep(container_of(task, &ManagedTask::task));
    }

    auto sleep(const uint64_t id) -> Error {
        const auto it = std::find_if(tasks.begin(), tasks.end(), [id](const auto& t) { return t->task.get_id() == id; });
        if(it == tasks.end()) {
            return Error::Code::NoSuchTask;
        }

        sleep(it->get());
        return Error::Code::Success;
    }

    auto wakeup(Task* const task, const int nice = -1) -> void {
        wakeup(container_of(task, &ManagedTask::task), nice);
    }

    auto wakeup(const uint64_t id, const int nice = -1) -> Error {
        const auto it = std::find_if(tasks.begin(), tasks.end(), [id](const auto& t) { return t->task.get_id() == id; });
        if(it == tasks.end()) {
            return Error::Code::NoSuchTask;
        }

        wakeup(it->get(), nice);
        return Error::Code::Success;
    }

    auto send_message(const uint64_t id, Message message) -> Error {
        const auto it = std::find_if(tasks.begin(), tasks.end(), [id](const auto& t) { return t->task.get_id() == id; });
        if(it == tasks.end()) {
            return Error::Code::NoSuchTask;
        }
        (*it)->task.send_message(std::move(message));
        return Error::Code::Success;
    }

    auto get_current_task() -> Task& {
        return running[current_nice].front()->task;
    }

    TaskManager() {
        auto& task   = new_managed_task();
        task.nice    = current_nice;
        task.running = true;
        running[current_nice].push_back(&task);

        auto& idle = new_managed_task();
        idle.task.init_context(idle_main, 0);
        idle.nice    = max_nice;
        idle.running = true;
        running[max_nice].push_back(&idle);
    }
};

inline auto task_manager = (TaskManager*)(nullptr);
inline auto kernel_task  = (Task*)(nullptr);

inline auto Task::sleep() -> Task& {
    task_manager->sleep(this);
    return *this;
}

inline auto Task::wakeup() -> Task& {
    task_manager->wakeup(this);
    return *this;
}
} // namespace task
