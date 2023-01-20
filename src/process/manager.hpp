#pragma once
#include <span>

#include "../interrupt/vector.hpp"
#include "../log.hpp"
#include "../message.hpp"
#include "../smp/ipi.hpp"
#include "../timer.hpp"
#include "../util/spinlock.hpp"
#include "process.hpp"

namespace process {
// assembly functions
extern "C" {
auto switch_context(const ThreadContext* next, ThreadContext* current, std::atomic_uint8_t* current_lock) -> void;
auto restore_context(const ThreadContext* next) -> void;
}

constexpr static auto max_nice     = Nice(2);
constexpr static auto invalid_nice = std::numeric_limits<Nice>::max();

constexpr static auto nice_to_index(const Nice nice) -> size_t {
    return nice + max_nice;
}

constexpr static auto index_to_nice(const size_t index) -> Nice {
    return Nice(index) - max_nice;
}

constexpr static auto is_valid_nice(const Nice nice) -> bool {
    return nice >= -max_nice && nice <= max_nice;
}

template <class T, class U>
auto erase_all(T& c, const U& value) -> void {
    const auto it = std::remove(c.begin(), c.end(), value);
    c.erase(it, c.end());
}

class ProcessorLocal {
  private:
    static auto should_skip(const Thread* const thread, const size_t tick) -> bool {
        if(thread->suspend_from == 0) {
            return false;
        }

        const auto elapsed = tick - thread->suspend_from;
        return elapsed < thread->suspend_for;
    }

  public:
    Thread*                                           this_thread;
    std::array<std::deque<Thread*>, max_nice * 2 + 1> run_queue;
    uint8_t                                           lapic_id;

    auto update_this_thread(const size_t tick) -> void {
        {
            auto& nice_queue = run_queue[nice_to_index(this_thread->nice)];
            erase_all(nice_queue, this_thread);
            if(this_thread->running_on != smp::invalid_processor_number) {
                nice_queue.push_back(this_thread);
            }
        }
        for(auto nice = -max_nice; nice <= max_nice; nice += 1) {
            for(const auto thread : run_queue[nice_to_index(nice)]) {
                if(!should_skip(thread, tick)) {
                    thread->suspend_from = 0;
                    this_thread          = thread;
                    return;
                }
            }
        }
        fatal_error("kernel: run queue empty");
    }

    auto move_between_run_queue(Thread* const thread, const Nice nice) -> Error {
        if(!is_valid_nice(nice)) {
            return Error::Code::InvalidNice;
        }
        if(thread->nice == nice) {
            return Success();
        }

        erase_from_run_queue(thread);
        thread->nice = nice;
        push_to_run_queue(thread);

        return Success();
    }

    auto push_to_run_queue(Thread* const thread) -> void {
        run_queue[nice_to_index(thread->nice)].push_back(thread);
    }

    auto erase_from_run_queue(Thread* const thread) -> void {
        erase_all(run_queue[nice_to_index(thread->nice)], thread);
    }
};

class Manager {
  private:
    uint64_t tick = 0;

    spinlock::SpinLock          mutex;
    IDMap<ProcessID, Process>   processes;
    std::vector<ProcessorLocal> locals;

    spinlock::SpinLock                                                events_mutex;
    dense_map::DenseMap<EventID, std::optional<std::vector<Thread*>>> events;

    const EventID thread_joined_event;
    const EventID process_joined_event;
    ProcessID     kernel_pid;
    Thread*       event_processor;

    paging::PML4Table& pml4_table;

    static auto idle_main(const uint64_t id, const int64_t data) -> void {
        while(true) {
            __asm__("hlt");
        }
    }

    auto switch_thread(AutoLock lock) -> void {
        auto& local = locals[smp::get_processor_number()];

        const auto current_thread = local.this_thread;
        local.update_this_thread(tick);
        const auto next_thread = local.this_thread;

        if(current_thread == next_thread) {
            return;
        }

        //        logger(LogLevel::Debug, "manager: context switch(%lu.%lu->%lu.%lu)", current_thread->process->id, current_thread->id, next_thread->process->id, next_thread->id);

        {
            const auto process = next_thread->process;
            const auto lock    = AutoLock(process->page_map_mutex);
            process->apply_page_map(lock, pml4_table);
        }
        alignas(16) const auto next_context = next_thread->context;
        switch_context(&next_context, &current_thread->context, lock.get_raw_mutex()->get_native());
    }

    auto switch_thread(AutoLock lock, ThreadContext& current_context, const bool continue_to_next) -> void {
        auto& local = locals[smp::get_processor_number()];

        const auto current_thread = local.this_thread;
        local.update_this_thread(tick);
        const auto next_thread = local.this_thread;

        if(current_thread == next_thread) {
            if(continue_to_next) {
                trigger_timer_interrupt_to_next_processor();
            }
            return;
        }

        // logger(LogLevel::Debug, "manager: context switch(%lu.%lu->%lu.%lu)", current_thread->process->id, current_thread->id, next_thread->process->id, next_thread->id);

        memcpy(&current_thread->context, &current_context, sizeof(ThreadContext));
        if(continue_to_next) {
            trigger_timer_interrupt_to_next_processor();
            lock.forget();
        } else {
            lock.release();
        }

        {
            const auto process = next_thread->process;
            const auto lock    = AutoLock(process->page_map_mutex);
            process->apply_page_map(lock, pml4_table);
        }
        restore_context(&next_thread->context);
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

    auto wakeup_thread(const AutoLock& lock, Thread* const thread, const Nice nice = invalid_nice) -> Error {
        auto& local = locals[smp::get_processor_number()];

        if(thread->running_on != smp::invalid_processor_number) {
            if(nice == invalid_nice) {
                return Success();
            } else {
                return local.move_between_run_queue(thread, nice);
            }
        }

        if(nice != invalid_nice) {
            if(!is_valid_nice(nice)) {
                return Error::Code::InvalidNice;
            }
            thread->nice = nice;
        }
        thread->running_on = smp::get_processor_number();
        local.push_to_run_queue(thread);
        return Success();
    }

    auto sleep_thread(AutoLock lock, Thread* const thread) -> void {
        if(thread->running_on == smp::invalid_processor_number) {
            return;
        }

        if(thread == locals[smp::get_processor_number()].this_thread) {
            thread->running_on = smp::invalid_processor_number;
            switch_thread(std::move(lock));
        } else {
            auto& local        = locals[thread->running_on];
            thread->running_on = smp::invalid_processor_number;
            local.erase_from_run_queue(thread);
        }
    }

    auto suspend_thread_for_tick(AutoLock lock, Thread* const thread, const size_t wait_tick) -> void {
        if(wait_tick == 0) {
            return;
        }

        thread->suspend_from = tick == 0 ? 1 : tick;
        thread->suspend_for  = tick == 0 ? wait_tick - 1 : wait_tick;
        if(thread == locals[smp::get_processor_number()].this_thread) {
            switch_thread(std::move(lock));
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

    auto wait_event(AutoLock lock, const EventID event_id) -> Error {
        auto& local = locals[smp::get_processor_number()];

        const auto events_lock = AutoLock(events_mutex);
        if(const auto e = push_thread_to_events(lock, events_lock, event_id, local.this_thread)) {
            return e;
        }
        sleep_thread(std::move(lock), local.this_thread);
        return Success();
    }

    auto unwait_event(const AutoLock& /*lock*/, const EventID event_id) -> Error {
        auto& local = locals[smp::get_processor_number()];
        if(!events.contains(event_id)) {
            return Error::Code::NoSuchEvent;
        } else {
            const auto thread = local.this_thread;
            erase_all(*events[event_id], thread);
            erase_all(thread->events, event_id);
        }

        return Success();
    }

    auto cancel_events_of_thread(const AutoLock& /*lock*/, const AutoLock& /*events_lock*/, Thread* const thread) -> void {
        for(const auto event_id : thread->events) {
            fatal_assert(events.contains(event_id), "process::manager: unknown event_id found in thread");
            erase_all(*events[event_id], thread);
        }
    }

    auto check_message_queue_and_wakeup_kernel(const AutoLock& lock) -> void {
        if(kernel_message_queue.empty()) {
            return;
        }
        fatal_assert(wakeup_thread(lock, event_processor) == Error::Code::Success, "failed to wakeup kernel thread");
    }

    static auto send_timer_ipi(const uint8_t lapic_id) -> void {
        volatile auto& lapic_registers = lapic::get_lapic_registers();

        auto command_low  = smp::InterruptCommandLow{.data = lapic_registers.interrupt_command_0 & 0xFF'F0'00'00u};
        auto command_high = smp::InterruptCommandHigh{.data = lapic_registers.interrupt_command_1 & 0x00'FF'FF'FFu};

        // clear apic error
        lapic_registers.error_status = 0;
        // create command
        command_low.bits.vector                = interrupt::Vector::LAPICTimer;
        command_low.bits.delivery_mode         = smp::DeliveryMode::Fixed;
        command_low.bits.destination_mode      = smp::DestinationMode::Physical;
        command_low.bits.level                 = smp::Level::Assert;
        command_low.bits.trigger_mode          = smp::TriggerMode::Level;
        command_low.bits.destination_shorthand = smp::DestinationShorthand::NoShorthand;
        command_high.bits.destination          = lapic_id;
        // send
        lapic_registers.interrupt_command_1 = command_high.data;
        lapic_registers.interrupt_command_0 = command_low.data;

        while(smp::InterruptCommandLow{.data = lapic_registers.interrupt_command_0}.bits.delivery_status == smp::DeliveryStatus::SendPending) {
            __asm__("pause");
        }
    }

    auto trigger_timer_interrupt_to_next_processor() -> void {
        const auto next_lapic_id = locals[smp::get_processor_number() + 1].lapic_id;
        send_timer_ipi(next_lapic_id);
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

    auto wakeup_thread(const ProcessID pid, const ThreadID tid, const Nice nice = invalid_nice) -> Error {
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
        auto  lock  = AutoLock(mutex);
        auto& local = locals[smp::get_processor_number()];

        sleep_thread(std::move(lock), local.this_thread);
    }

    auto suspend_thread_for_ms(const ProcessID pid, const ThreadID tid, const size_t ms) -> Error {
        auto lock = AutoLock(mutex);

        const auto find_thread_result = find_alive_thread(lock, pid, tid);
        if(!find_thread_result) {
            return find_thread_result.as_error();
        }
        const auto thread = find_thread_result.as_value();

        suspend_thread_for_tick(std::move(lock), thread, ms * timer::frequency / 1000);
        return Success();
    }

    auto suspend_this_thread_for_ms(const size_t ms) -> void {
        suspend_thread_for_tick(AutoLock(mutex), locals[smp::get_processor_number()].this_thread, ms * timer::frequency / 1000);
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
        auto& local = locals[smp::get_processor_number()];
        exit_thread(AutoLock(mutex), local.this_thread);
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
            if(const auto e = wait_event(std::move(lock), thread_joined_event)) {
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
            if(const auto e = wait_event(std::move(lock), thread_joined_event)) {
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
        return wait_event(AutoLock(mutex), event_id);
    }

    auto wait_events(const std::span<EventID> event_ids) -> Error {
        auto  lock  = AutoLock(mutex);
        auto& local = locals[smp::get_processor_number()];

        const auto events_lock = AutoLock(events_mutex);
        for(const auto event_id : event_ids) {
            if(const auto e = push_thread_to_events(lock, events_lock, event_id, local.this_thread)) {
                return e;
            }
        }
        sleep_thread(std::move(lock), local.this_thread);
        return Success();
    }

    auto unwait_event(const EventID event_id) -> Error {
        auto lock = AutoLock(mutex);
        return unwait_event(lock, event_id);
    }

    auto unwait_events(const std::span<EventID> event_ids) -> Error {
        auto lock = AutoLock(mutex);
        for(const auto event_id : event_ids) {
            if(const auto e = unwait_event(lock, event_id)) {
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
            erase_all(t->events, event_id);
            if(const auto e = wakeup_thread(lock, t)) {
                return e;
            }
        }
        return Success();
    }

    auto get_this_thread() -> Thread* {
        auto& local = locals[smp::get_processor_number()];
        return local.this_thread;
    }

    // for kernel processes
    auto expand_locals(const size_t new_size) -> void {
        locals.resize(new_size);
    }

    auto capture_context() -> void {
        auto& local    = locals[smp::get_processor_number()];
        local.lapic_id = lapic::read_lapic_id();

        // capture this context
        {
            const auto tid_result = create_thread(kernel_pid);
            fatal_assert(tid_result, "failed to create kernel thread");
            const auto tid = tid_result.as_value();
            fatal_assert(wakeup_thread(kernel_pid, tid, -max_nice) == Error::Code::Success, "failed to wakeup kernel thread");
            {
                const auto lock               = AutoLock(mutex);
                const auto find_thread_result = find_alive_thread(lock, kernel_pid, tid);
                fatal_assert(find_thread_result, "missing kernel thread");
                const auto thread = find_thread_result.as_value();
                thread->movable   = false;
                local.this_thread = thread;
            }
        }

        // create idle thread
        {
            const auto tid_result = create_thread(kernel_pid, idle_main, 0);
            fatal_assert(tid_result, "failed to create idle thread");
            const auto tid = tid_result.as_value();
            fatal_assert(wakeup_thread(kernel_pid, tid, max_nice) == Error::Code::Success, "failed to wakeup idle thread");
            {
                const auto lock               = AutoLock(mutex);
                const auto find_thread_result = find_alive_thread(lock, kernel_pid, tid);
                fatal_assert(find_thread_result, "missing idle thread");
                const auto thread = find_thread_result.as_value();
                thread->movable   = false;
            }
        }
    }
    // ~for kernel processes

    // for interrupt handlers
    auto migrate_threads(const AutoLock& /*lock*/) -> void {
        auto local_thread_num = std::vector<size_t>(locals.size());
        auto total_threads    = size_t(0);

        for(auto i = size_t(0); i < locals.size(); i += 1) {
            auto& local = locals[i];
            for(auto& q : local.run_queue) {
                local_thread_num[i] += q.size();
            }
            total_threads += local_thread_num[i];
        }
        const auto average = total_threads / locals.size();
        const auto mod     = total_threads - average * locals.size();
        auto       thread  = (Thread*)(nullptr);
    loop:
        auto exit = true;
        for(auto i = size_t(0); i < locals.size(); i += 1) {
            const auto threshold = i < mod ? average + 1 : average;
            auto&      local     = locals[i];
            if(thread == nullptr) {
                if(local_thread_num[i] < threshold) {
                    continue;
                }
                for(auto q = local.run_queue.rbegin(); q != local.run_queue.rend(); q += 1) {
                    for(const auto t : *q) {
                        if(!t->movable || t == local.this_thread) {
                            continue;
                        }
                        thread = t;
                        local.erase_from_run_queue(t);
                        local_thread_num[i] -= 1;
                        exit = false;
                        break;
                    }
                    if(thread != nullptr) {
                        break;
                    }
                }
            } else {
                if(local_thread_num[i] >= threshold) {
                    continue;
                }
                thread->running_on = i;
                local.push_to_run_queue(thread);
                thread = nullptr;
                local_thread_num[i] += 1;
            }
        }
        if(!exit) {
            goto loop;
        }
    }
    auto switch_thread_may_fail(ThreadContext& current_context) -> void {
        const auto lapic_id = lapic::read_lapic_id();
        if(lapic_id == smp::first_lapic_id) {
            tick += 1;
            if(!mutex.try_aquire()) {
                return;
            }
        }

        auto lock = AutoLock(mutex, mutex_like::locked_mutex);

        if(lapic_id == smp::first_lapic_id) {
            check_message_queue_and_wakeup_kernel(lock);
            if(tick % 500 == 0) {
                migrate_threads(lock);
            }
        }
        const auto continue_to_next = lapic_id != smp::last_lapic_id;
        switch_thread(std::move(lock), current_context, continue_to_next);
    }

    auto post_kernel_message(Message message) -> void {
        kernel_message_queue.push(message);

        if(!mutex.try_aquire()) {
            return;
        }
        auto lock = AutoLock(mutex, mutex_like::locked_mutex);
        fatal_assert(wakeup_thread(lock, event_processor) == Error::Code::Success, "failed to wakeup kernel thread");
    }

    auto post_kernel_message_with_cli(Message message) -> void {
        __asm__("cli");
        post_kernel_message(std::move(message));
        __asm__("sti");
    }
    // ~for interrupt handlers

    Manager(paging::PML4Table& pml4_table) : thread_joined_event(create_event()),
                                             process_joined_event(create_event()),
                                             pml4_table(pml4_table) {
        locals.resize(1);
        auto& local = locals[smp::get_processor_number()];

        kernel_pid = create_process();
        capture_context();
        event_processor = local.this_thread;
    }
};

inline auto manager = (Manager*)(nullptr);
} // namespace process
