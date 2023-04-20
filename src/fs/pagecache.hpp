#pragma once
#include "../memory/frame.hpp"
#include "../mutex.hpp"

namespace fs {
struct CachePage {
    enum class State {
        Uninitialized,
        Clean,
        Dirty,
    };

    memory::SmartSingleFrameID page;
    State                      state = State::Uninitialized;

    auto get_frame() -> std::byte* {
        return static_cast<std::byte*>(page->get_frame());
    }
};

class CacheProvider {
  private:
  public:
    virtual auto lock() -> SmartMutex                 = 0;
    virtual auto at(size_t index) -> CachePage&       = 0;
    virtual auto get_capacity() const -> size_t       = 0;
    virtual auto ensure_capacity(size_t size) -> void = 0;

    virtual ~CacheProvider() {}
};

class DefaultCacheProvider : public CacheProvider {
  private:
    Mutex                  mutex;
    std::vector<CachePage> cache;

  public:
    auto lock() -> SmartMutex override {
        return SmartMutex(mutex);
    }

    auto at(const size_t index) -> CachePage& override {
        return cache[index];
    }

    auto get_capacity() const -> size_t override {
        return cache.size();
    }

    auto ensure_capacity(const size_t size) -> void override {
        cache.resize(size);
    }
};
} // namespace fs
