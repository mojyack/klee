#pragma once
#include "mutex-like.hpp"
#include "task/manager.hpp"

class Mutex {
  private:
    std::atomic_flag flag;

  public:
    auto aquire() -> void {
        while(flag.test_and_set()) {
            task::task_manager->get_current_task().wait_address(this);
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
        task::task_manager->notify_address(this);
    }

    Mutex() {
        task::task_manager->add_wait_address(this);
    }

    ~Mutex() {
        task::task_manager->erase_wait_address(this);
    }
};

using SmartMutex = AutoMutex<Mutex>;

template <class T>
using Critical = SharedValue<Mutex, T>;

class Event {
  private:
    std::atomic_flag flag;

  public:
    auto wait() -> void {
        while(!flag.test()) {
            task::task_manager->get_current_task().wait_address(this);
        }
    }

    auto notify() -> void {
        flag.test_and_set();
        task::task_manager->notify_address(this);
    }

    auto reset() -> void {
        flag.clear();
    }

    Event() {
        task::task_manager->add_wait_address(this);
    }

    ~Event() {
        task::task_manager->erase_wait_address(this);
    }
};
