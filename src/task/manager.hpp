#pragma once
#include "task.hpp"

#include "../asmcode.h"
#include "../error.hpp"
#include "../mutex-like.hpp"
#include "../print.hpp"
#include "../util/container-of.hpp"

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

    std::atomic<Task*>                                       lock;
    std::atomic<Task*>                                       self_task; // mirror of running[current_nice].front()
    std::vector<std::unique_ptr<ManagedTask>>                tasks;
    std::array<std::deque<ManagedTask*>, max_nice + 1>       running;
    uint64_t                                                 last_id      = 0;
    int                                                      current_nice = 0;
    bool                                                     nice_changed = false;
    std::unordered_map<uintptr_t, std::vector<ManagedTask*>> wait_address_tasks;

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
        notify_address_locked(this);
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
            wait_address(this, container_of(self_task.load(), &ManagedTask::task));
        }
    }

    // this functions unlocks lock
    auto sleep(ManagedTask* const task) -> void {
        if(!task->running) {
            release_lock();
            return;
        }
        task->running = false;

        if(task != running[current_nice].front()) {
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
    auto wait_address(const void* const address, ManagedTask* const task) -> void {
        const auto key = reinterpret_cast<uintptr_t>(address);
        const auto p   = wait_address_tasks.find(key);
        if(p == wait_address_tasks.end()) {
            // bug
            release_lock();
            return;
        }
        p->second.emplace_back(task);
        sleep(task);
    }

    auto notify_address_locked(const void* const address) -> void {
        const auto key = reinterpret_cast<uintptr_t>(address);
        const auto p   = wait_address_tasks.find(key);
        if(p != wait_address_tasks.end()) {
            const auto to_notify = std::move(p->second);
            for(auto task : to_notify) {
                wakeup(task, -1);
            }
        }
    }

    auto switch_task_locked(Task* const next_task) -> void {
        next_task->apply_page_map();
        auto current_task = self_task.load();
        __asm__("cli");
        self_task.store(next_task);
        switch_context(&next_task->get_context(), &current_task->get_context());
    }

    // this function unlocks lock
    auto switch_task_locked(const bool sleep_current = false) -> void {
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
        next_task->task.apply_page_map();
        __asm__("cli");
        release_lock();
        self_task.store(&next_task->task);
        switch_context(&next_task->task.get_context(), &current_task->task.get_context());
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
            switch_task_locked(expected);
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

    // called by timer interrupt
    auto switch_task_may_fail() -> void {
        if(!try_aquire_lock()) {
            return;
        }
        switch_task_locked(false);
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

    auto add_wait_address(const void* const address) -> void {
        aquire_lock();
        const auto key = reinterpret_cast<uintptr_t>(address);
        if(wait_address_tasks.find(key) == wait_address_tasks.end()) {
            wait_address_tasks.emplace(key, std::vector<ManagedTask*>());
        }
        release_lock();
    }

    auto erase_wait_address(const void* const address) -> void {
        aquire_lock();
        const auto key = reinterpret_cast<uintptr_t>(address);
        const auto p   = wait_address_tasks.find(key);
        if(p != wait_address_tasks.end()) {
            wait_address_tasks.erase(p);
        }
        release_lock();
    }

    auto wait_address(const void* const address, Task* const task) -> void {
        aquire_lock();
        wait_address(address, container_of(task, &ManagedTask::task));
    }

    auto notify_address(const void* const address) -> void {
        aquire_lock();
        notify_address_locked(address);
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

        wait_address_tasks.emplace(reinterpret_cast<uintptr_t>(this), std::vector<ManagedTask*>());
    }
};

inline auto task_manager = (TaskManager*)(nullptr);

inline auto Task::exit() -> void {
    task_manager->exit_task(this);
}

inline auto Task::sleep() -> Task& {
    task_manager->sleep(this);
    return *this;
}

inline auto Task::wakeup(const int nice) -> Task& {
    task_manager->wakeup(this, nice);
    return *this;
}

inline auto Task::wakeup_may_fail() -> void {
    task_manager->wakeup_may_fail(this);
}

inline auto Task::wait_address(const void* const address) -> void {
    task_manager->wait_address(address, this);
}

inline auto kernel_task = (Task*)(nullptr);
} // namespace task
