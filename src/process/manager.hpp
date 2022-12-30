#pragma once
#include <span>

#include "../log.hpp"
#include "../message.hpp"
#include "../util/spinlock.hpp"
#include "process.hpp"

namespace process {
// assembly functions
extern "C" {
auto switch_context(const ThreadContext* next, ThreadContext* current, std::atomic_uint8_t* current_lock) -> void;
auto restore_context(const ThreadContext* next) -> void;
}

class Manager {
  public:
    constexpr static auto max_nice     = Nice(2);
    constexpr static auto invalid_nice = std::numeric_limits<Nice>::max();

  private:
    template <class T, class U>
    static auto erase(T& c, const U& value) -> void {
        const auto it = std::remove(c.begin(), c.end(), value);
        c.erase(it, c.end());
    }

    constexpr static auto nice_to_index(const Nice nice) -> size_t {
        return nice + max_nice;
    }

    constexpr static auto index_to_nice(const size_t index) -> Nice {
        return Nice(index) - max_nice;
    }

    constexpr static auto is_valid_nice(const Nice nice) -> bool {
        return nice >= -max_nice && nice <= max_nice;
    }

    spinlock::SpinLock                                mutex;
    IDMap<ProcessID, Process>                         processes;
    Thread*                                           this_thread;
    std::array<std::deque<Thread*>, max_nice * 2 + 1> run_queue;
    Nice                                              current_nice       = 0;
    bool                                              reset_current_nice = false;

    spinlock::SpinLock                                                events_mutex;
    dense_map::DenseMap<EventID, std::optional<std::vector<Thread*>>> events;

    const EventID thread_joined_event;
    const EventID process_joined_event;
    Thread*       kernel_thread;

    static auto idle_main(const uint64_t id, const int64_t data) -> void {
        while(true) {
            __asm__("hlt");
        }
    }

    auto switch_thread(AutoLock lock) -> void {
        const auto current_thread = this_thread;
        update_this_thread(lock);
        const auto next_thread = this_thread;

        if(current_thread == next_thread) {
            return;
        }

        //        logger(LogLevel::Debug, "manager: context switch(%lu.%lu->%lu.%lu)", current_thread->process->id, current_thread->id, next_thread->process->id, next_thread->id);

        {
            const auto process = next_thread->process;
            const auto lock    = AutoLock(process->page_map_mutex);
            process->apply_page_map(lock);
        }
        alignas(16) const auto next_context = next_thread->context;
        switch_context(&next_context, &current_thread->context, lock.get_raw_mutex()->get_native());
    }

    auto switch_thread(AutoLock lock, ThreadContext& current_context) -> void {
        const auto current_thread = this_thread;
        update_this_thread(lock);
        const auto next_thread = this_thread;

        if(current_thread == next_thread) {
            return;
        }

        //        logger(LogLevel::Debug, "manager: context switch(%lu.%lu->%lu.%lu)", current_thread->process->id, current_thread->id, next_thread->process->id, next_thread->id);

        memcpy(&current_thread->context, &current_context, sizeof(ThreadContext));

        {
            const auto process = next_thread->process;
            const auto lock    = AutoLock(process->page_map_mutex);
            process->apply_page_map(lock);
        }
        const auto next_context = next_thread->context;
        lock.release();
        restore_context(&next_context);
    }

    auto update_this_thread(const AutoLock& /*lock*/) -> void {
        {
            auto& nice_queue = run_queue[nice_to_index(current_nice)];
            nice_queue.pop_front();
            if(this_thread->running) {
                nice_queue.push_back(this_thread);
            }
            if(nice_queue.empty()) {
                reset_current_nice = true;
            }
        }
        if(reset_current_nice) {
            reset_current_nice = false;
            for(auto i = 0; i < 2 * max_nice + 1; i += 1) {
                if(run_queue[i].empty()) {
                    continue;
                }
                current_nice = index_to_nice(i);
                break;
            }
        }
        this_thread = run_queue[nice_to_index(current_nice)].front();
    }

    auto find_thread(const AutoLock& /*lock*/, const ProcessID pid, const ThreadID tid) -> Result<Thread*> {
        if(!processes.contains(pid)) {
            return Error::Code::NoSuchProcess;
        }
        const auto process = processes[pid].get();
        if(!process->threads.contains(tid)) {
            return Error::Code::NoSuchThread;
        }
        return process->threads[tid].get();
    }

    auto find_alive_thread(const AutoLock& lock, const ProcessID pid, const ThreadID tid) -> Result<Thread*> {
        const auto find_thread_result = find_thread(lock, pid, tid);
        if(!find_thread_result) {
            return find_thread_result.as_error();
        }

        const auto thread = find_thread_result.as_value();
        if(thread->zombie) {
            return Error::Code::DeadThread;
        }
        return thread;
    }

    auto create_thread(const AutoLock& /*lock*/, const ProcessID pid) -> Result<Thread*> {
        const auto lock = AutoLock(mutex);

        if(!processes.contains(pid)) {
            return Error::Code::NoSuchProcess;
        }

        const auto process = processes[pid].get();
        auto&      threads = process->threads;
        const auto tid     = threads.find_empty_slot();
        const auto thread  = new Thread(tid, process);
        threads[tid].reset(thread);
        return thread;
    }

    auto push_thread_to_events(const AutoLock& /*lock*/, const AutoLock& /*event_lock*/, const EventID event_id, Thread* const thread) -> Error {
        if(!events.contains(event_id)) {
            return Error::Code::NoSuchEvent;
        } else {
            events[event_id]->push_back(thread);
            thread->events.push_back(event_id);
        }
        return Success();
    }

    auto change_nice_of_running_thread(const AutoLock& /*lock*/, Thread* const thread, Nice nice) -> Error {
        if(nice == invalid_nice || thread->nice == nice) {
            return Success();
        }
        if(!is_valid_nice(nice)) {
            return Error::Code::InvalidNice;
        }

        auto& nice_queue = run_queue[nice_to_index(nice)];
        erase(run_queue[nice_to_index(thread->nice)], thread);
        nice_queue.push_back(thread);
        thread->nice = nice;

        if(thread == nice_queue.front()) {
            current_nice = nice;
            if(nice > current_nice) {
                reset_current_nice = true;
            }
        } else {
            if(nice < current_nice) {
                reset_current_nice = true;
            }
        }
        return Success();
    }

    auto wakeup_thread(const AutoLock& lock, Thread* const thread, Nice nice = invalid_nice) -> Error {
        if(thread->running) {
            return change_nice_of_running_thread(lock, thread, nice);
        }

        if(nice != invalid_nice) {
            if(!is_valid_nice(nice)) {
                return Error::Code::InvalidNice;
            }
            thread->nice = nice;
        } else {
            nice = thread->nice;
        }
        thread->running = true;
        run_queue[nice_to_index(nice)].push_back(thread);
        if(nice < current_nice) {
            reset_current_nice = true;
        }
        return Success();
    }

    auto sleep_thread(AutoLock lock, Thread* const thread) -> void {
        if(!thread->running) {
            return;
        }

        //        logger(LogLevel::Debug, "manager: sleeping thread(%lu.%lu)", thread->process->id, thread->id);

        thread->running = false;
        if(thread == this_thread) {
            switch_thread(std::move(lock));
        } else {
            erase(run_queue[nice_to_index(current_nice)], thread);
        }
    }

    auto exit_thread(AutoLock lock, Thread* const thread) -> void {
        thread->zombie = true;
        {
            const auto events_lock = AutoLock(events_mutex);
            cancel_events_of_thread(lock, events_lock, thread);
        }
        logger(LogLevel::Debug, "process: thread exitted(%lu.%lu)\n", thread->process->id, thread->id);
        sleep_thread(std::move(lock), thread);
    }

    auto wait_event(AutoLock lock, Thread* const thread, const EventID event_id) -> Error {
        const auto events_lock = AutoLock(events_mutex);
        if(const auto e = push_thread_to_events(lock, events_lock, event_id, thread)) {
            return e;
        }
        sleep_thread(std::move(lock), thread);
        return Success();
    }

    auto wait_events(AutoLock lock, Thread* const thread, const std::span<EventID> event_ids) -> Error {
        const auto events_lock = AutoLock(events_mutex);
        for(const auto event_id : event_ids) {
            if(const auto e = push_thread_to_events(lock, events_lock, event_id, thread)) {
                return e;
            }
        }
        sleep_thread(std::move(lock), thread);
        return Success();
    }

    auto unwait_event(const AutoLock& /*lock*/, Thread* const thread, const EventID event_id) -> Error {
        if(!events.contains(event_id)) {
            return Error::Code::NoSuchEvent;
        } else {
            erase(*events[event_id], thread);
            erase(thread->events, event_id);
        }

        return Success();
    }

    auto cancel_events_of_thread(const AutoLock& /*lock*/, const AutoLock& /*events_lock*/, Thread* const thread) -> void {
        for(const auto event_id : thread->events) {
            fatal_assert(events.contains(event_id), "process::manager: unknown event_id found in thread");
            erase(*events[event_id], thread);
        }
    }

    auto check_message_queue_and_wakeup_kernel(const AutoLock& lock) -> void {
        if(kernel_message_queue.empty()) {
            return;
        }
        fatal_assert(wakeup_thread(lock, kernel_thread) == Error::Code::Success, "failed to wakeup kernel thread");
    }

  public:
    auto create_process() -> ProcessID {
        const auto lock = AutoLock(mutex);

        const auto pid = processes.find_empty_slot();
        processes[pid].reset(new Process(pid));

        logger(LogLevel::Debug, "process: process created(%lu)\n", pid);
        return pid;
    }

    auto create_thread(const ProcessID pid) -> Result<ThreadID> {
        const auto lock = AutoLock(mutex);

        if(const auto r = create_thread(lock, pid); !r) {
            return r.as_error();
        } else {
            const auto thread = r.as_value();
            logger(LogLevel::Debug, "process: thread created(%lu.%lu)\n", pid, thread->id);
            return thread->id;
        }
    }

    auto create_thread(const ProcessID pid, ThreadEntry* const func, const int64_t data) -> Result<ThreadID> {
        const auto lock = AutoLock(mutex);

        if(const auto r = create_thread(lock, pid); !r) {
            return r.as_error();
        } else {
            const auto thread = r.as_value();
            thread->init_context(func, data);
            logger(LogLevel::Debug, "process: thread created with context(%lu.%lu)\n", pid, thread->id);
            return thread->id;
        }
    }

    auto wakeup_thread(const ProcessID pid, const ThreadID tid, Nice nice = invalid_nice) -> Error {
        const auto lock = AutoLock(mutex);

        const auto find_thread_result = find_alive_thread(lock, pid, tid);
        if(!find_thread_result) {
            return find_thread_result.as_error();
        }
        const auto thread = find_thread_result.as_value();

        return wakeup_thread(lock, thread);
    }

    auto sleep_thread(const ProcessID pid, const ThreadID tid) -> Error {
        auto lock = AutoLock(mutex);

        const auto find_thread_result = find_thread(lock, pid, tid);
        if(!find_thread_result) {
            return find_thread_result.as_error();
        }

        sleep_thread(std::move(lock), find_thread_result.as_value());
        return Success();
    }

    auto sleep_this_thread() -> void {
        auto lock = AutoLock(mutex);

        sleep_thread(std::move(lock), this_thread);
    }

    auto exit_thread(const ProcessID pid, const ThreadID tid) -> Error {
        auto lock = AutoLock(mutex);

        const auto find_thread_result = find_alive_thread(lock, pid, tid);
        if(!find_thread_result) {
            return find_thread_result.as_error();
        }
        const auto thread = find_thread_result.as_value();

        exit_thread(std::move(lock), thread);
        return Success();
    }

    auto exit_this_thread() -> void {
        exit_thread(AutoLock(mutex), this_thread);
    }

    auto wait_thread(const ProcessID pid, const ThreadID tid) -> Error {
    loop:
        auto lock = AutoLock(mutex);

        const auto find_thread_result = find_thread(lock, pid, tid);
        if(!find_thread_result) {
            return find_thread_result.as_error();
        }
        const auto thread = find_thread_result.as_value();

        if(!thread->zombie) {
            if(const auto e = wait_event(std::move(lock), this_thread, thread_joined_event)) {
                return e;
            }
            goto loop;
        }

        const auto process = thread->process;
        process->threads[thread->id].reset();
        return Success();
    }

    auto wait_process(const ProcessID pid) -> Error {
    loop:
        auto lock = AutoLock(mutex);

        if(!processes.contains(pid)) {
            return Error::Code::NoSuchProcess;
        }
        auto& process = processes[pid];

        if(!process->threads.empty()) {
            if(const auto e = wait_event(std::move(lock), this_thread, thread_joined_event)) {
                return e;
            }
            goto loop;
        }

        process.reset();
        return Success();
    }

    auto create_event() -> EventID {
        const auto events_lock = AutoLock(events_mutex);

        const auto event_id = events.find_empty_slot();
        events[event_id].emplace();
        return event_id;
    }

    auto delete_event(const EventID event_id) -> Error {
        const auto events_lock = AutoLock(events_mutex);

        if(!events.contains(event_id)) {
            return Error::Code::NoSuchEvent;
        }
        if(!events[event_id]->empty()) {
            logger(LogLevel::Error, "process: cannot delete event %u because this event is still used by...\n", event_id);
            for(auto t : *events[event_id]) {
                logger(LogLevel::Error, "  thread (%u.%u)\n", t->process->id, t->id);
            }
            return Error::Code::UnFinishedEvent;
        }
        events[event_id].reset();
        return Success();
    }

    auto wait_event(const EventID event_id) -> Error {
        auto lock = AutoLock(mutex);

        return wait_event(std::move(lock), this_thread, event_id);
    }

    auto wait_events(const std::span<EventID> events) -> Error {
        auto lock = AutoLock(mutex);

        return wait_events(std::move(lock), this_thread, events);
    }

    auto wait_event(const ProcessID pid, const ThreadID tid, const EventID event_id) -> Error {
        auto lock = AutoLock(mutex);

        const auto find_thread_result = find_alive_thread(lock, pid, tid);
        if(!find_thread_result) {
            return find_thread_result.as_error();
        }
        const auto thread = find_thread_result.as_value();

        return wait_event(std::move(lock), thread, event_id);
    }

    auto wait_events(const ProcessID pid, const ThreadID tid, const std::span<EventID> event_ids) -> Error {
        auto lock = AutoLock(mutex);

        const auto find_thread_result = find_alive_thread(lock, pid, tid);
        if(!find_thread_result) {
            return find_thread_result.as_error();
        }
        const auto thread = find_thread_result.as_value();

        return wait_events(std::move(lock), thread, event_ids);
    }

    auto unwait_event(const EventID event_id) -> Error {
        auto lock = AutoLock(mutex);
        return unwait_event(lock, this_thread, event_id);
    }

    auto unwait_events(const std::span<EventID> event_ids) -> Error {
        auto lock = AutoLock(mutex);
        for(const auto event_id : event_ids) {
            if(const auto e = unwait_event(lock, this_thread, event_id)) {
                return e;
            }
        }
        return Success();
    }

    auto notify_event(const EventID event_id) -> Error {
        const auto lock = AutoLock(mutex);

        auto to_wakeup = std::vector<Thread*>();
        {
            const auto events_lock = AutoLock(events_mutex);

            if(!events.contains(event_id)) {
                return Error::Code::NoSuchEvent;
            } else {
                std::swap(*events[event_id], to_wakeup);
            }
        }

        for(const auto t : to_wakeup) {
            erase(t->events, event_id);
            if(const auto e = wakeup_thread(lock, t)) {
                return e;
            }
        }
        return Success();
    }

    auto get_this_thread() -> Thread* {
        return this_thread;
    }
    auto post_kernel_message_with_cli(Message message) -> void {
        __asm__("cli");
        post_kernel_message(std::move(message));
        __asm__("sti");
    }

    // for interrupt handlers
    auto switch_thread_may_fail(ThreadContext& current_context) -> void {
        if(!mutex.try_aquire()) {
            return;
        }
        auto lock = AutoLock(mutex, mutex_like::locked_mutex);
        check_message_queue_and_wakeup_kernel(lock);
        switch_thread(std::move(lock), current_context);
    }

    auto post_kernel_message(Message message) -> void {
        kernel_message_queue.push(message);

        if(!mutex.try_aquire()) {
            return;
        }
        auto lock = AutoLock(mutex, mutex_like::locked_mutex);
        fatal_assert(wakeup_thread(lock, kernel_thread) == Error::Code::Success, "failed to wakeup kernel thread");
    }

    Manager() : thread_joined_event(create_event()),
                process_joined_event(create_event()) {
        {
            const auto pid        = create_process();
            const auto tid_result = create_thread(pid);
            fatal_assert(tid_result, "failed to create kernel thread");
            const auto tid = tid_result.as_value();
            fatal_assert(wakeup_thread(pid, tid, -max_nice) == Error::Code::Success, "failed to wakeup kernel thread");
            {
                const auto lock               = AutoLock(mutex);
                const auto find_thread_result = find_alive_thread(lock, pid, tid);
                fatal_assert(find_thread_result, "missing kernel thread");
                this_thread = find_thread_result.as_value();
            }
            kernel_thread = this_thread;
        }
        {
            const auto pid        = this_thread->process->id;
            const auto tid_result = create_thread(pid, idle_main, 0);
            fatal_assert(tid_result, "failed to create idle thread");
            const auto tid = tid_result.as_value();
            fatal_assert(wakeup_thread(pid, tid, max_nice) == Error::Code::Success, "failed to wakeup idle thread");
        }
    }
};

inline auto manager = (Manager*)(nullptr);
} // namespace process
