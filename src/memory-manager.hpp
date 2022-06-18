#pragma once
#include <array>
#include <limits>

#include "error.hpp"

#define MM_DEBUG_PRINT 0
#if MM_DEBUG_PRINT == 1
#include "framebuffer.hpp"
#endif

constexpr auto operator""_KiB(const unsigned long long kib) -> unsigned long long {
    return kib * 1024;
}

constexpr auto operator""_MiB(const unsigned long long mib) -> unsigned long long {
    return mib * 1024_KiB;
}

constexpr auto operator""_GiB(const unsigned long long gib) -> unsigned long long {
    return gib * 1024_MiB;
}

static constexpr auto bytes_per_frame = 4_KiB;

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

    explicit FrameID(const size_t id) : id(id) {}
};

inline auto nullframe = FrameID(std::numeric_limits<size_t>::max());

class BitmapMemoryManager {
  private:
    using MaplineType = uint64_t;

    static constexpr auto max_physical_memory_bytes = 128_GiB;
    static constexpr auto required_frames           = max_physical_memory_bytes / bytes_per_frame;
    static constexpr auto bits_per_mapline          = 8 * sizeof(MaplineType);

    std::array<MaplineType, required_frames / bits_per_mapline> allocation_map;
    FrameID                                                     range_begin = FrameID(0);
    FrameID                                                     range_end   = FrameID(required_frames);

    auto get_bit(const FrameID frame) const -> bool {
        const auto line_index = frame.get_id() / bits_per_mapline;
        const auto bit_index  = frame.get_id() % bits_per_mapline;
        return (allocation_map[line_index] & (static_cast<MaplineType>(1) << bit_index)) != 0;
    }

    auto set_bit(const FrameID frame, const bool flag) -> void {
        const auto line_index = frame.get_id() / bits_per_mapline;
        const auto bit_index  = frame.get_id() % bits_per_mapline;
        if(flag) {
            allocation_map[line_index] |= (static_cast<MaplineType>(1) << bit_index);
        } else {
            allocation_map[line_index] &= (static_cast<MaplineType>(1) << bit_index);
        }
    }

  public:
    auto set_bits(const FrameID begin, const size_t frames, const bool flag) -> void {
        for(auto i = size_t(0); i < frames; i += 1) {
            set_bit(FrameID(begin.get_id() + i), flag);
        }
    }

    auto allocate(const size_t frames) -> Result<FrameID> {
        auto start_frame_id = range_begin.get_id();
    loop:
        auto i = size_t(0);
        for(; i < frames; i += 1) {
            if(start_frame_id + i > range_end.get_id()) {
                return Error::Code::NoEnoughMemory;
            }
            if(get_bit(FrameID(start_frame_id + i))) {
                break;
            }
        }
        if(i == frames) {
            auto r = FrameID(start_frame_id);
            set_bits(r, frames, true);
            return r;
        }
        start_frame_id += i + 1;
        goto loop;
    }

    auto deallocate(const FrameID begin, const size_t frames) -> Error {
        set_bits(begin, frames, false);
        return Error::Code::Success;
    }

    auto set_range(const FrameID begin, const FrameID end) -> void {
        range_begin = begin;
        range_end   = end;
    }

#if MM_DEBUG_PRINT == 1
    auto print_bitmap(const FramebufferConfig& config, const Rectangle rect, const int size) -> void {
        auto fb = Framebuffer<PixelBGRResv8BitPerColor>(config);
        for(auto y = rect.a.y; y - rect.a.y < rect.width() && y < config.vertical_resolution; y += size) {
            for(auto x = rect.a.x; x - rect.a.x < rect.width() && x < config.horizontal_resolution; x += size) {
                const auto frame = FrameID(range_begin.get_id() + (y / size) * rect.width() + (x / size));
                if(frame.get_id() >= range_end.get_id()) {
                    return;
                }
                fb.write_rect({x, y}, {x + size, y + size}, static_cast<uint8_t>(get_bit(frame) ? 0x40 : 0xFF));
            }
        }
    }
#endif
};

inline auto allocator = (BitmapMemoryManager*)(nullptr);

#undef MM_DEBUG_PRINT
