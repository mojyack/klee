#pragma once
#include <deque>
#include <queue>

#include "acpi.hpp"
#include "interrupt-vector.hpp"
#include "message.hpp"

namespace timer {
enum Flags {
    None     = 0,
    Periodic = 1 << 0,
    Task     = 1 << 1,
};

struct Timer {
    uint64_t origin;
    uint64_t timeout;
    int      value;
    Flags    flags;

    auto operator<(const Timer& o) const -> bool {
        return timeout > o.timeout;
    }

    template <class T, class U>
    Timer(const T timeout, const int value, const U flags) : Timer(static_cast<uint64_t>(timeout), value, static_cast<Flags>(flags)) {}

    template <>
    Timer(const uint64_t timeout, const int value, const Flags flags) : timeout(timeout), value(value), flags(flags) {}
};

class TimerManager {
  private:
    uint64_t                   tick = 0;
    std::deque<Message>&       main_queue;
    std::priority_queue<Timer> timers;

  public:
    auto count_tick() -> bool {
        tick += 1;

        auto task_switch = false;
        while(true) {
            const auto& t = timers.top();
            if(t.origin + t.timeout > tick) {
                break;
            }

            timers.pop();
            if(t.flags & Flags::Periodic) {
                add_timer(t);
            }
            if(t.flags & Flags::Task) {
                task_switch = true;
            } else {
                auto m             = Message(MessageType::Timer);
                m.data.timer.value = t.value;
                main_queue.push_back(m);
            }
        }
        return task_switch;
    }

    auto get_tick() const -> uint64_t {
        return tick;
    }

    auto add_timer(Timer timer) -> void {
        timer.origin = tick;
        timers.push(timer);
    }

    TimerManager(std::deque<Message>& main_queue) : main_queue(main_queue) {}
};

namespace internal {
constexpr auto             count_max     = 0xFFFFFFFFu;
constexpr auto             timer_freq    = 100;
volatile inline const auto lvt_timer     = reinterpret_cast<uint32_t*>(0xFEE00320);
volatile inline const auto initial_count = reinterpret_cast<uint32_t*>(0xFEE00380);
volatile inline const auto current_count = reinterpret_cast<uint32_t*>(0xFEE00390);
volatile inline const auto divide_config = reinterpret_cast<uint32_t*>(0xFEE003E0);
} // namespace internal

inline auto start() -> void {
    *internal::initial_count = internal::count_max;
}

inline auto get_elapsed() -> uint32_t {
    return internal::count_max - *internal::current_count;
}

inline auto stop() -> void {
    *internal::initial_count = 0;
}

inline auto lapic_timer_freq = uint64_t();

inline auto initialize_timer() -> void {
    *internal::divide_config = 0b1011;      // divide 1:1
    *internal::lvt_timer     = 0b001 << 16; // masked, one-shot

    start();
    acpi::wait_miliseconds(100);
    const auto elapsed = get_elapsed();
    stop();
    lapic_timer_freq = static_cast<uint64_t>(elapsed) * 10;

    *internal::divide_config = 0b1011;                                                 // divide 1:1
    *internal::lvt_timer     = (0b010 << 16) | interrupt::InterruptVector::LAPICTimer; // not-masked, periodic
    *internal::initial_count = lapic_timer_freq / internal::timer_freq;
}
} // namespace timer
