#pragma once
#include <vector>

#include "mutex-like.hpp"
#include "spinlock.hpp"

template <class T>
class CriticalQueue {
  private:
    mutex_like::SharedValue<spinlock::SpinLock, std::vector<T>> buffer[2];
    std::atomic_int                                             flip     = 0;
    std::atomic_bool                                            is_empty = true;

  public:
    template <class... Args>
    auto push(Args&&... args) -> void {
        auto [lock, data] = buffer[flip].access();
        data.emplace_back(std::forward<Args>(args)...);
        is_empty = false;
    }

    auto push(T item) -> void {
        auto [lock, data] = buffer[flip].access();
        data.emplace_back(std::move(item));
        is_empty = false;
    }

    auto swap() -> std::vector<T>& {
        buffer[!flip].unsafe_access().clear();

        flip ^= 1;
        is_empty = true;

        auto [lock, data] = buffer[!flip].access();
        return data;
    }

    auto empty() -> bool {
        return is_empty;
    }
};
