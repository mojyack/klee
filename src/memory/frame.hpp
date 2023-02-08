#pragma once
#include <limits>
#include <utility>

namespace memory {
constexpr auto operator""_KiB(const unsigned long long kib) -> size_t {
    return kib * 1024;
}

constexpr auto operator""_MiB(const unsigned long long mib) -> size_t {
    return mib * 1024_KiB;
}

constexpr auto operator""_GiB(const unsigned long long gib) -> size_t {
    return gib * 1024_MiB;
}

constexpr auto bytes_per_frame = 4_KiB;

class FrameID {
  private:
    size_t id;

  public:
    auto get_id() const -> size_t {
        return id;
    }

    auto get_frame() const -> void* {
        return reinterpret_cast<void*>(id * bytes_per_frame);
    }

    auto operator+(const size_t value) const -> FrameID {
        return FrameID(id + value);
    }

    auto operator!=(const FrameID& o) const -> bool {
        return id != o.id;
    }

    explicit FrameID(const size_t id) : id(id) {}
};

inline auto nullframe = FrameID(std::numeric_limits<size_t>::max());

class SmartFrameID {
  private:
    FrameID id = nullframe;
    size_t  frames;

  public:
    auto free() -> void;

    auto get_frames() const -> size_t {
        return frames;
    }

    auto operator=(SmartFrameID&& o) -> SmartFrameID& {
        free();
        std::swap(id, o.id);
        frames = o.frames;
        return *this;
    }

    auto operator->() -> FrameID* {
        return &id;
    }

    auto operator*() -> FrameID {
        return id;
    }

    operator bool() const {
        return id != nullframe;
    }

    SmartFrameID(SmartFrameID&& o) {
        *this = std::move(o);
    }

    SmartFrameID() = default;

    SmartFrameID(const FrameID id, const size_t frames) : id(id), frames(frames) {}

    ~SmartFrameID() {
        free();
    }
};

class SmartSingleFrameID {
  private:
    FrameID id = nullframe;

  public:
    auto free() -> void;

    auto operator=(SmartSingleFrameID&& o) -> SmartSingleFrameID& {
        free();
        std::swap(id, o.id);
        return *this;
    }

    auto operator->() -> FrameID* {
        return &id;
    }

    auto operator*() -> FrameID {
        return id;
    }

    operator bool() const {
        return id != nullframe;
    }

    SmartSingleFrameID(SmartSingleFrameID&& o) {
        *this = std::move(o);
    }

    SmartSingleFrameID() = default;

    SmartSingleFrameID(const FrameID id) : id(id) {}

    ~SmartSingleFrameID() {
        free();
    }
};
} // namespace memory
