#pragma once
#include "task.hpp"

class Mutex {
  private:
    std::atomic_flag flag;

  public:
    auto aquire() -> void {
        while(flag.test_and_set()) {
            task::task_manager->get_current_task().sleep();
        }
    }

    auto release() -> void {
        flag.clear();
    }
};

class SmartMutex {
  private:
    Mutex* mutex = nullptr;

    auto release() -> void {
        if(mutex != nullptr) {
            mutex->release();
        }
    }

  public:
    SmartMutex(SmartMutex&& o) {
        release();
        mutex   = o.mutex;
        o.mutex = nullptr;
    }

    SmartMutex(Mutex& mutex) : SmartMutex(&mutex) {}

    SmartMutex(Mutex* const mutex) : mutex(mutex) {
        mutex->aquire();
    }

    ~SmartMutex() {
        release();
    }
};

template <class T>
class Critical {
  private:
    Mutex mutex;
    T     data;

  public:
    auto access() -> std::pair<SmartMutex, T&> {
        return {SmartMutex(mutex), data};
    }

    template <class... Args>
    Critical(Args&&... args) : T(std::move(args)...) {}
    Critical() {}
};

class Event {
  private:
    task::Task*      task;
    std::atomic_flag flag;

  public:
    auto wait() -> void {
        flag.clear();
        while(!flag.test()) {
            task->sleep();
        }
    }
    auto notify() -> void {
        flag.test_and_set();
        task->wakeup();
    }

    Event(): task(&task::task_manager->get_current_task()) {}
    Event(task::Task& task) : task(&task) {}
};
