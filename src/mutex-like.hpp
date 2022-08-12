#pragma once
#include <optional>

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

    auto release() -> void {
        if(mutex != nullptr) {
            mutex->release();
        }
    }

  public:
    AutoMutex(AutoMutex&& o) {
        release();
        mutex   = o.mutex;
        o.mutex = nullptr;
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
    Mutex mutex;
    T     data;

  public:
    auto access() -> std::pair<AutoMutex<Mutex>, T&> {
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

    template <class... Args>
    SharedValue(Args&&... args) : data(std::move(args)...) {}
    SharedValue() {}
};
