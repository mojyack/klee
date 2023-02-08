#pragma once
#include <array>
#include <limits>

#include "../error.hpp"
#include "../libc-support.hpp"
#include "../mutex.hpp"
#include "../panic.hpp"
#include "frame.hpp"
#include "memory-type.hpp"

namespace memory {
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
    auto allocate(const size_t frames) -> Result<SmartFrameID> {
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
            return SmartFrameID(r, frames);
        }
        start_frame_id += i + 1;
        goto loop;
    }

    auto allocate_single() -> Result<SmartSingleFrameID> {
        for(auto i = range_begin.get_id(); i <= range_end.get_id(); i += 1) {
            const auto id = FrameID(i);
            if(!get_bit(id)) {
                set_bit(id, true);
                return SmartSingleFrameID(id);
            }
        }
        return Error::Code::NoEnoughMemory;
    }

    auto deallocate(const FrameID begin, const size_t frames) -> Error {
        set_bits(begin, frames, false);
        return Success();
    }

    auto is_available(const size_t address) -> bool {
        const auto frame = address / bytes_per_frame;
        return !get_bit(FrameID(frame));
    }

    auto is_available(const FrameID frame) -> bool {
        return !get_bit(frame);
    }

    auto initialize_heap() -> Result<SmartFrameID> {
        constexpr auto heap_frames = 64 * 512;

        auto heap_start_r = allocate(heap_frames);
        if(!heap_start_r) {
            return heap_start_r.as_error();
        }
        auto& heap_start = heap_start_r.as_value();

        program_break     = reinterpret_cast<caddr_t>(heap_start->get_id() * bytes_per_frame);
        program_break_end = program_break + heap_frames * bytes_per_frame;
        return std::move(heap_start);
    }

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

inline auto critical_allocator = (Critical<BitmapMemoryManager*>*)(nullptr);

inline auto allocate(const size_t frames) -> Result<SmartFrameID> {
    auto [lock, allocator] = critical_allocator->access();
    return allocator->allocate(frames);
}

inline auto allocate_single() -> Result<SmartSingleFrameID> {
    auto [lock, allocator] = critical_allocator->access();
    return allocator->allocate_single();
}

inline auto SmartFrameID::free() -> void {
    if(id != nullframe) {
        auto [lock, allocator] = critical_allocator->access();
        fatal_assert(!allocator->deallocate(id, frames), "failed to deallocate memory");
        id = nullframe;
    }
}

inline auto SmartSingleFrameID::free() -> void {
    if(id != nullframe) {
        auto [lock, allocator] = critical_allocator->access();
        fatal_assert(!allocator->deallocate(id, 1), "failed to deallocate memory");
        id = nullframe;
    }
}
} // namespace memory
