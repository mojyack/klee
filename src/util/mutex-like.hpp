#pragma once
#include <optional>

namespace mutex_like {
template <class T>
concept MutexLike = requires(T& mutex) {
                        mutex.aquire();
                        mutex.try_aquire();
                        mutex.release();
                    };

using LockedMutex           = bool;
constexpr auto locked_mutex = LockedMutex(true);

template <MutexLike Mutex>
class AutoMutex {
  private:
    Mutex* mutex = nullptr;

  public:
    auto release() -> void {
        if(mutex != nullptr) {
            mutex->release();
        }
    }

    auto forget() -> void {
        mutex = nullptr;
    }

    auto get_raw_mutex() -> Mutex* {
        return mutex;
    }

    AutoMutex(AutoMutex&& o) {
        release();
        mutex = std::exchange(o.mutex, nullptr);
    }

    AutoMutex(Mutex& mutex) : mutex(&mutex) {
        mutex.aquire();
    }

    AutoMutex(Mutex& mutex, const LockedMutex) : mutex(&mutex) {}

    ~AutoMutex() {
        release();
    }
};

template <MutexLike Mutex, class T>
class SharedValue {
  private:
    mutable Mutex mutex;
    T             data;

  public:
    auto access() -> std::pair<AutoMutex<Mutex>, T&> {
        return {AutoMutex(mutex), data};
    }

    auto access() const -> std::pair<AutoMutex<Mutex>, const T&> {
        return {AutoMutex(mutex), data};
    }

    auto try_access() -> std::optional<std::pair<AutoMutex<Mutex>, T&>> {
        if(mutex.try_aquire()) {
            return std::pair<AutoMutex<Mutex>, T&>{AutoMutex(mutex, locked_mutex), data};
        }
        return std::nullopt;
    }

    auto unsafe_access() -> T& {
        return data;
    }

    auto assume_locked() -> T& {
        return data;
    }

    SharedValue() {
    }

    SharedValue(SharedValue&& other) : mutex(std::move(other.mutex)),
                                       data(std::move(other.data)) {
    }

    template <class... Args>
    SharedValue(Args&&... args) : data(std::forward<Args>(args)...) {
    }
};
} // namespace mutex_like
