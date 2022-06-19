#pragma once
#include <cstdint>

namespace timer {
class LAPICTimer {
  private:
    static constexpr auto             count_max     = 0xFFFFFFFFu;
    volatile static inline const auto lvt_timer     = reinterpret_cast<uint32_t*>(0xFEE00320);
    volatile static inline const auto initial_count = reinterpret_cast<uint32_t*>(0xFEE00380);
    volatile static inline const auto current_count = reinterpret_cast<uint32_t*>(0xFEE00390);
    volatile static inline const auto divide_config = reinterpret_cast<uint32_t*>(0xFEE003E0);

  public:
    auto start() -> void {
        *initial_count = count_max;
    }

    auto get_elapsed() const -> uint32_t {
        return count_max - *current_count;
    }

    auto stop() -> void {
        *initial_count = 0;
    }

    LAPICTimer() {
        *divide_config = 0b1011;             // divide 1:1
        *lvt_timer     = (0b001 << 16) | 32; // masked, one-shot
    }
};
} // namespace timer
