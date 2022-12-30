#pragma once
#include "mutex-like.hpp"

namespace spinlock {
class SpinLock {
  private:
    std::atomic_uint8_t flag;

  public:
    auto aquire() -> void {
        auto expected = uint8_t(0);
        while(!flag.compare_exchange_weak(expected, 1)) {
            __asm__("pause");
        }
    }

    auto try_aquire() -> bool {
        auto expected = uint8_t(0);
        return flag.compare_exchange_weak(expected, 1);
    }

    auto release() -> void {
        flag.store(0);
    }

    auto get_native() -> std::atomic_uint8_t* {
        return &flag;
    }
};
} // namespace spinlock
