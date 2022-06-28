#pragma once
#include <array>
#include <limits>

#include "error.hpp"
#include "libc-support.hpp"
#include "memory-map.h"

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

    auto operator+(const size_t value) const -> FrameID {
        return FrameID(id + value);
    }

    auto operator!=(const FrameID& o) const -> bool {
        return id != o.id;
    }

    explicit FrameID(const size_t id) : id(id) {}
};

inline auto nullframe = FrameID(std::numeric_limits<size_t>::max());

class BitmapMemoryManager {
  private:
    using MaplineType = uint64_t;

    static constexpr auto max_physical_memory_bytes = 32_GiB;
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

    auto set_bits(const FrameID begin, const size_t frames, const bool flag) -> void {
        for(auto i = size_t(0); i < frames; i += 1) {
            set_bit(FrameID(begin.get_id() + i), flag);
        }
    }

    auto set_range(const FrameID begin, const FrameID end) -> void {
        range_begin = begin;
        range_end   = end;
    }

  public:
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

    auto initialize_heap() -> Error {
        constexpr auto heap_frames = 64 * 512;

        const auto heap_start = allocate(heap_frames);
        if(!heap_start) {
            return heap_start.as_error();
        }

        program_break     = reinterpret_cast<caddr_t>(heap_start.as_value().get_id() * bytes_per_frame);
        program_break_end = program_break + heap_frames * bytes_per_frame;
        return Error::Code::Success;
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

    BitmapMemoryManager(const MemoryMap& memory_map) {
        const auto memory_map_base = reinterpret_cast<uintptr_t>(memory_map.buffer);
        auto       available_end   = uintptr_t(0);
        for(auto iter = memory_map_base; iter < memory_map_base + memory_map.map_size; iter += memory_map.descriptor_size) {
            const auto& desc = *reinterpret_cast<MemoryDescriptor*>(iter);
            if(available_end < desc.physical_start) {
                set_bits(FrameID(available_end / bytes_per_frame), (desc.physical_start - available_end) / bytes_per_frame, true);
            }

            const auto physical_end = desc.physical_start + desc.number_of_pages * uefi_page_size;
            if(is_available_memory_type(static_cast<MemoryType>(desc.type))) {
                available_end = physical_end;
            } else {
                set_bits(FrameID(desc.physical_start / bytes_per_frame), desc.number_of_pages * uefi_page_size / bytes_per_frame, true);
            }
        }
        set_range(FrameID(1), FrameID(available_end / bytes_per_frame));
    }
};

#undef MM_DEBUG_PRINT

inline auto allocator = (BitmapMemoryManager*)(nullptr);

class SmartFrameID {
  private:
    FrameID id = nullframe;
    size_t  frames;

  public:
    auto operator=(SmartFrameID&& o) -> SmartFrameID& {
        if(id != nullframe) {
            allocator->deallocate(id, frames);
        }

        id     = o.id;
        frames = o.frames;
        o.id   = nullframe;
        return *this;
    }

    auto operator->() -> FrameID* {
        return &id;
    }

    auto operator*() -> FrameID {
        return id;
    }

    SmartFrameID(SmartFrameID&& o) {
        *this = std::move(o);
    }

    SmartFrameID() = default;
    SmartFrameID(const FrameID id, const size_t frames) : id(id), frames(frames) {}
    ~SmartFrameID() {
        if(id != nullframe) {
            allocator->deallocate(id, frames);
        }
    }
};
