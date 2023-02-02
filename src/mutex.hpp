#pragma once
#include "log.hpp"
#include "process/manager.hpp"
#include "util/mutex-like.hpp"

class Mutex {
  private:
    std::atomic_flag flag;
    process::EventID id;

    auto erase() -> void {
        if(id != process::invalid_event) {
            if(const auto e = process::manager->delete_event(id)) {
                logger(LogLevel::Error, "mutex: failed to delete event %lu(%lu)\n", id, e.as_int());
            }
        }
    }

  public:
    auto aquire() -> void {
        while(flag.test_and_set()) {
            if(const auto e = process::manager->wait_event(id)) {
                logger(LogLevel::Error, "mutex: failed to wait event %lu(%lu)\n", id, e.as_int());
            }
        }
    }

    auto try_aquire() -> bool {
        if(flag.test_and_set()) {
            return false;
        }
        return true;
    }

    auto release() -> void {
        flag.clear();
        if(const auto e = process::manager->notify_event(id)) {
            logger(LogLevel::Error, "mutex: failed to notify event %lu(%lu)\n", id, e.as_int());
        }
    }

    auto operator=(Mutex&& o) -> Mutex& {
        erase();
        id   = o.id;
        o.id = process::invalid_event;
        return *this;
    }

    Mutex(Mutex&& o) {
        *this = std::move(o);
    }

    Mutex() : id(process::manager->create_event()) {}

    ~Mutex() {
        erase();
    }
};

using SmartMutex = mutex_like::AutoMutex<Mutex>;

template <class T>
using Critical = mutex_like::SharedValue<Mutex, T>;

class Event {
  private:
    std::atomic_flag flag;
    process::EventID id;

    auto erase() -> void {
        if(id != process::invalid_event) {
            if(const auto e = process::manager->delete_event(id)) {
                logger(LogLevel::Error, "mutex: failed to delete event %lu(%lu)\n", id, e.as_int());
            }
        }
    }

  public:
    auto wait() -> void {
        while(!flag.test()) {
            if(const auto e = process::manager->wait_event(id)) {
                logger(LogLevel::Error, "mutex: failed to wait event %lu(%lu)\n", id, e.as_int());
            }
        }
    }

    auto notify() -> void {
        if(!flag.test_and_set()) {
            if(const auto e = process::manager->notify_event(id)) {
                logger(LogLevel::Error, "mutex: failed to notify event %lu(%lu)\n", id, e.as_int());
            }
        }
    }

    auto reset() -> void {
        flag.clear();
    }

    auto is_valid() const -> bool {
        return id != process::invalid_event;
    }

    auto test() const -> bool {
        return flag.test();
    }

    auto read_id() const -> process::EventID {
        return id;
    }

    auto operator=(Event&& o) -> Event& {
        erase();
        id   = o.id;
        o.id = process::invalid_event;
        return *this;
    }

    Event(Event&& o) {
        *this = std::move(o);
    }

    Event() : id(process::manager->create_event()) {}

    ~Event() {
        erase();
    }
};
