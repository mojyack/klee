#pragma once
#include <cstdint>
#include <queue>

#include "acpi.hpp"
#include "interrupt.hpp"

namespace timer {
class Timer {
  private:
    unsigned int timeout;
    int          value;

  public:
    auto get_timeout() const -> unsigned int {
        return timeout;
    }

    auto get_value() const -> int {
        return value;
    }

    auto operator<(const Timer& o) const -> bool {
        return timeout > o.timeout;
    }

    Timer(const unsigned int timeout, const int value) : timeout(timeout), value(value) {}
};

class TimerManager {
  private:
    std::priority_queue<Timer> timers;
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
