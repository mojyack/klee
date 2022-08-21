#pragma once
#include "../asmcode.h"
#include "../debug.hpp"
#include "../error.hpp"
#include "../mutex-like.hpp"
#include "../util/container-of.hpp"
#include "task.hpp"

extern "C" uint64_t self_task_system_stack;

namespace task {
class TaskManager {
  private:
    constexpr static auto max_nice = 4;

    struct ManagedTask {
        Task     task;
        uint32_t nice    = max_nice / 2;
        bool     running = false;

        ManagedTask(const uint64_t id) : task(id) {}
    };

    std::atomic<Task*>                                 lock;
    std::atomic<Task*>                                 self_task; // mirror of running[current_nice].front()
    std::vector<std::unique_ptr<ManagedTask>>          tasks;
    std::array<std::deque<ManagedTask*>, max_nice + 1> running;
    uint64_t                                           last_id      = 0;
    int                                                current_nice = 0;
    bool                                               nice_changed = false;

    std::atomic_uint64_t                                    last_event_id = 1; // event_id(0) is reserved for manager
    std::unordered_map<uint64_t, std::vector<ManagedTask*>> events;

    std::vector<ManagedTask*> delete_queue;

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

    // private functions are all thread unsafe!

    auto update_self_task(Task& task) -> void {
        self_task.store(&task);
        self_task_system_stack = task.get_system_stack_pointer();
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

    // this functions unlocks lock
    auto exit_managed_task(ManagedTask* const task) -> void {
        delete_queue.emplace_back(task);
        notify_event_locked(0);
        sleep(task);
    }

    // this function is thread safe
    auto wait_managed_task(ManagedTask* const task) -> void {
        while(true) {
            aquire_lock();
            auto found = false;
            for(auto i = delete_queue.begin(); i < delete_queue.end(); i += 1) {
                if(*i == task) {
                    found = true;
                    i     = delete_queue.erase(i);
                }
            }
            if(found) {
                for(auto i = tasks.begin(); i < tasks.end(); i += 1) {
                    if(i->get() == task) {
                        tasks.erase(i);
                        break;
                    }
                }
                release_lock();
                return;
            }
            wait_event(0, container_of(self_task.load(), &ManagedTask::task));
        }
    }

    // this function unlocks lock
    auto sleep(ManagedTask* const task) -> void {
        if(!task->running) {
            release_lock();
            return;
        }
        task->running = false;

        if(&task->task != self_task.load()) {
            erase(running[task->nice], task);
            release_lock();
            return;
        }
        switch_task_locked(true);
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

    // this functions unlocks lock
    auto wait_event(const uint64_t event_id, ManagedTask* const task) -> void {
        events[event_id].emplace_back(task);
        sleep(task);
    }

    // this functions unlocks lock
    auto wait_events(const std::vector<uint64_t> event_ids, ManagedTask* const task) -> void {
        for(const auto id : event_ids) {
            events[id].emplace_back(task);
        }
        sleep(task);
    }

    auto notify_event_locked(const uint64_t event_id) -> void {
        const auto p = events.find(event_id);
        if(p != events.end()) {
            const auto to_notify = std::move(p->second);
            for(auto task : to_notify) {
                wakeup(task, -1);
            }
        }
    }

    auto rotate_run_queue(const bool sleep_current) -> ManagedTask& {
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

        return *current_task;
    }

    // this function unlocks lock
    auto switch_task_locked(const bool sleep_current = false) -> void {
        auto       current_task = &rotate_run_queue(sleep_current);
        const auto next_task    = running[current_nice].front();

        if(current_task == next_task) {
            release_lock();
            return;
        }

        next_task->task.apply_page_map();
        __asm__("cli");
        release_lock();
        update_self_task(next_task->task);
        next_task->task.get_context().rflags |= 0b1000000000; // interrupt enable
        switch_context(&next_task->task.get_context(), &current_task->task.get_context());
    }

    // this function unlocks lock
    auto switch_task_locked(TaskContext& context) -> void {
        const auto current_task = &rotate_run_queue(false);
        const auto next_task    = running[current_nice].front();
        memcpy(&current_task->task.get_context(), &context, sizeof(TaskContext));

        if(current_task == next_task) {
            release_lock();
            return;
        }

        next_task->task.apply_page_map();
        __asm__("cli");
        release_lock();
        update_self_task(next_task->task);
        next_task->task.get_context().rflags |= 0b1000000000; // interrupt enable
        restore_context(&next_task->task.get_context());
    }

    auto try_aquire_lock() -> bool {
        auto expected = (Task*)nullptr;
        return lock.compare_exchange_weak(expected, self_task);
    }

    auto aquire_lock() -> void {
        while(true) {
            auto expected = (Task*)nullptr;
            if(lock.compare_exchange_weak(expected, self_task)) {
                return;
            }
            switch_task(expected);
        }
    }

    auto release_lock() -> void {
        lock.store(nullptr);
    }

  public:
    auto new_task() -> Task& {
        aquire_lock();
        auto& task = new_managed_task().task;
        release_lock();
        return task;
    }

    auto exit_task(Task* const task) -> void {
        aquire_lock();
        exit_managed_task(container_of(task, &ManagedTask::task));
    }

    auto wait_task(Task* const task) -> void {
        wait_managed_task(container_of(task, &ManagedTask::task));
    }

    auto switch_task(const bool sleep_current = false) -> void {
        aquire_lock();
        switch_task_locked(sleep_current);
    }

    // this function is thread-safe
    auto switch_task(Task* const next_task) -> void {
        next_task->apply_page_map();
        auto current_task = self_task.load();
        __asm__("cli");
        update_self_task(*next_task);
        next_task->get_context().rflags |= 0b1000000000; // interrupt enable
        switch_context(&next_task->get_context(), &current_task->get_context());
    }

    // called by timer interrupt
    auto switch_task_may_fail(TaskContext& context) -> void {
        if(!try_aquire_lock()) {
            return;
        }
        switch_task_locked(context);
    }

    auto sleep(Task* const task) -> void {
        aquire_lock();
        sleep(container_of(task, &ManagedTask::task));
    }

    auto sleep(const uint64_t id) -> Error {
        aquire_lock();
        const auto it = std::find_if(tasks.begin(), tasks.end(), [id](const auto& t) { return t->task.get_id() == id; });
        if(it == tasks.end()) {
            release_lock();
            return Error::Code::NoSuchTask;
        }
        sleep(it->get());
        return Error::Code::Success;
    }

    auto wakeup(Task* const task, const int nice = -1) -> void {
        aquire_lock();
        wakeup(container_of(task, &ManagedTask::task), nice);
        release_lock();
    }

    auto wakeup_may_fail(Task* const task) -> void {
        if(!try_aquire_lock()) {
            return;
        }
        wakeup(container_of(task, &ManagedTask::task), -1);
        release_lock();
    }

    auto wakeup(const uint64_t id, const int nice = -1) -> Error {
        aquire_lock();
        const auto it    = std::find_if(tasks.begin(), tasks.end(), [id](const auto& t) { return t->task.get_id() == id; });
        const auto found = it != tasks.end();
        if(found) {
            wakeup(it->get(), nice);
        }
        release_lock();
        return found ? Error() : Error::Code::NoSuchTask;
    }

    auto send_message(const uint64_t id, Message message) -> Error {
        aquire_lock();
        const auto it    = std::find_if(tasks.begin(), tasks.end(), [id](const auto& t) { return t->task.get_id() == id; });
        const auto found = it != tasks.end();
        if(found) {
            (*it)->task.send_message(std::move(message));
        }
        release_lock();
        return found ? Error() : Error::Code::NoSuchTask;
    }

    auto get_current_task() -> Task& {
        return *self_task.load();
    }

    auto new_event() -> uint64_t {
        return last_event_id.fetch_add(1);
    }

    auto delete_event(const uint64_t event_id) -> void {
        aquire_lock();
        events.erase(event_id);
        release_lock();
    }

    auto wait_event(const uint64_t event_id, Task* const task) -> void {
        aquire_lock();
        wait_event(event_id, container_of(task, &ManagedTask::task));
    }

    auto wait_events(std::vector<uint64_t> event_ids, Task* const task) -> void {
        aquire_lock();
        wait_events(std::move(event_ids), container_of(task, &ManagedTask::task));
    }

    auto notify_event(const uint64_t event_id) -> void {
        aquire_lock();
        notify_event_locked(event_id);
        release_lock();
    }

    TaskManager() {
        auto& task = new_managed_task();
        self_task.store(&task.task);
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

inline auto manager = (TaskManager*)(nullptr);
inline auto kernel_task = (Task*)(nullptr);
} // namespace task
