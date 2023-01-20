#pragma once
#include <deque>
#include <queue>

#include "acpi.hpp"
#include "freq.hpp"
#include "interrupt/vector.hpp"
#include "lapic.hpp"

namespace timer {
namespace internal {
inline auto measure_count_for_100ms() -> uint64_t {
    constexpr auto count_max = uint32_t(-1);

    auto& lapic_registers                = lapic::get_lapic_registers();
    lapic_registers.divide_configuration = 0b1011;      // divide 1:1
    lapic_registers.lvt_timer            = 0b001 << 16; // masked, one-shot
    lapic_registers.initial_count        = count_max;

    acpi::wait_miliseconds(100);

    const auto elapsed            = count_max - lapic_registers.current_count;
    lapic_registers.initial_count = 0;
    return elapsed;
};
} // namespace internal

inline auto initialize_timer() -> void {
    const auto elapsed          = internal::measure_count_for_100ms();
    const auto lapic_timer_freq = static_cast<uint64_t>(elapsed) * 10; // tick per second

    auto& lapic_registers = lapic::get_lapic_registers();

    lapic_registers.divide_configuration = 0b1011;                                        // divide 1:1
    lapic_registers.lvt_timer            = (0b010 << 16) | interrupt::Vector::LAPICTimer; // not-masked, periodic
    lapic_registers.initial_count        = lapic_timer_freq / frequency;
}
} // namespace timer
