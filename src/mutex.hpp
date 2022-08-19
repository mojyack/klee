#pragma once
#include "mutex-like.hpp"
#include "task/manager.hpp"

class Mutex {
  private:
    std::atomic_flag flag;
    uint64_t         id = 0;

    auto erase() -> void {
        if(id != 0) {
            task::manager->delete_event(id);
        }
    }

  public:
    auto aquire() -> void {
        while(flag.test_and_set()) {
            task::manager->get_current_task().wait_event(id);
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
        task::manager->notify_event(id);
    }

    auto operator=(Mutex&& o) -> Mutex& {
        erase();
        id   = o.id;
        o.id = 0;
        return *this;
    }

    Mutex(Mutex&& o) {
        *this = std::move(o);
    }

    Mutex() : id(task::manager->new_event()) {}

    ~Mutex() {
        erase();
    }
};

using SmartMutex = AutoMutex<Mutex>;

template <class T>
using Critical = SharedValue<Mutex, T>;

class Event {
  private:
    std::atomic_flag flag;
    uint64_t         id = 0;

    auto erase() -> void {
        if(id != 0) {
            task::manager->delete_event(id);
        }
    }

  public:
    auto wait() -> void {
        while(!flag.test()) {
            task::manager->get_current_task().wait_event(id);
        }
    }

    auto notify() -> void {
        if(!flag.test_and_set()) {
            task::manager->notify_event(id);
        }
    }

    auto reset() -> void {
        flag.clear();
    }

    auto is_valid() const -> bool {
        return id != 0;
    }

    auto test() const -> bool {
        return flag.test();
    }

    auto read_id() const -> uint64_t {
        return id;
    }

    auto operator=(Event&& o) -> Event& {
        erase();
        id   = o.id;
        o.id = 0;
        return *this;
    }

    Event(Event&& o) {
        *this = std::move(o);
    }

    Event() : id(task::manager->new_event()) {}

    ~Event() {
        erase();
    }
};
