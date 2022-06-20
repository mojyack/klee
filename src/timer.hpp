#pragma once
#include <cstdint>

#include "interrupt.hpp"

namespace timer {
namespace internal {
constexpr auto             count_max     = 0xFFFFFFFFu;
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

inline auto initialize_timer() -> void {
    *internal::divide_config = 0b1011;                                                 // divide 1:1
    *internal::lvt_timer     = (0b010 << 16) | interrupt::InterruptVector::LAPICTimer; // not-masked, periodic
    *internal::initial_count = internal::count_max;
}
} // namespace timer
