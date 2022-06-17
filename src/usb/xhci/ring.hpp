#pragma once
#include "../../error.hpp"
#include "../memory.hpp"
#include "registers.hpp"
#include "trb.hpp"

namespace usb::xhci {
class Ring {
  private:
    TRB*   buffer      = nullptr;
    size_t buffer_size = 0;
    bool   cycle_bit;
    size_t write_index;

    auto copy_to_last(const std::array<uint32_t, 4>& data) -> void {
        // data[0..2] must be written prior to data[3].
        for(auto i = 0; i < 3; i += 1) {
            buffer[write_index].data[i] = data[i];
        }
        buffer[write_index].data[3] = (data[3] & 0xFFFFFFFEu) | static_cast<uint32_t>(cycle_bit);
    }

    auto push(const std::array<uint32_t, 4>& data) -> TRB* {
        const auto trb_pointer = &buffer[write_index];
        copy_to_last(data);
        write_index += 1;
        if(write_index == buffer_size - 1) {
            auto link              = LinkTRB(buffer);
            link.bits.toggle_cycle = true;
            copy_to_last(link.data);
            write_index = 0;
            cycle_bit   = !cycle_bit;
        }
        return trb_pointer;
    }

  public:
    auto initialize(const size_t buf_size) -> Error {
        if(buffer != nullptr) {
            deallocate_memory(buffer);
        }
        cycle_bit   = true;
        write_index = 0;
        buffer_size = buf_size;
        buffer      = allocate_array<TRB>(buffer_size, 64, 64 * 1024);
        if(buffer == nullptr) {
            return Error::Code::NoEnoughMemory;
        }
        memset(buffer, 0, buffer_size * sizeof(TRB));
        return Error::Code::Success;
    }

    template <typename T>
    auto push(const T& trb) -> TRB* {
        return push(trb.data);
    }

    auto get_buffer() const -> TRB* {
        return buffer;
    }

    Ring& operator=(const Ring&) = delete;
    Ring()                       = default;
    Ring(const Ring&)            = delete;
    ~Ring() {
        if(buffer != nullptr) {
            deallocate_memory(buffer);
        }
    }
};

union EventRingSegmentTableEntry {
    std::array<uint32_t, 4> data;
    struct {
        uint64_t ring_segment_base_address; // 64 bytes alignment

        uint32_t ring_segment_size : 16;
        uint32_t : 16;

        uint32_t : 32;
    } __attribute__((packed)) bits;
};

class EventRing {
  private:
    TRB*   buffer;
    size_t buffer_size;

    bool                        cycle_bit;
    EventRingSegmentTableEntry* entry;
    InterrupterRegisterSet*     interrupter;

  public:
    auto initialize(const size_t buffer_size, InterrupterRegisterSet* const interrupter) -> Error {
        if(buffer != nullptr) {
            deallocate_memory(buffer);
        }

        cycle_bit         = true;
        this->buffer_size = buffer_size;
        this->interrupter = interrupter;

        buffer = allocate_array<TRB>(buffer_size, 64, 64 * 1024);
        if(buffer == nullptr) {
            return Error::Code::NoEnoughMemory;
        }
        memset(buffer, 0, buffer_size * sizeof(TRB));

        entry = allocate_array<EventRingSegmentTableEntry>(1, 64, 64 * 1024);
        if(entry == nullptr) {
            deallocate_memory(buffer);
            return Error::Code::NoEnoughMemory;
        }
        memset(entry, 0, 1 * sizeof(EventRingSegmentTableEntry));

        entry[0].bits.ring_segment_base_address = reinterpret_cast<uint64_t>(buffer);
        entry[0].bits.ring_segment_size         = buffer_size;

        auto erstsz = interrupter->erstsz.read();
        erstsz.set_size(1);
        interrupter->erstsz.write(erstsz);

        write_dequeue_pointer(&buffer[0]);

        auto erstba = interrupter->erstba.read();
        erstba.set_pointer(reinterpret_cast<uint64_t>(entry));
        interrupter->erstba.write(erstba);

        return Error::Code::Success;
    }

    auto read_dequeue_pointer() const -> TRB* {
        return reinterpret_cast<TRB*>(interrupter->erdp.read().get_pointer());
    }

    auto write_dequeue_pointer(TRB* const p) -> void {
        auto erdp = interrupter->erdp.read();
        erdp.set_pointer(reinterpret_cast<uint64_t>(p));
        interrupter->erdp.write(erdp);
    }

    auto has_front() const -> bool {
        return get_front()->bits.cycle_bit == cycle_bit;
    }

    auto get_front() const -> TRB* {
        return read_dequeue_pointer();
    }

    auto pop() -> void {
        const auto segment_begin = reinterpret_cast<TRB*>(entry[0].bits.ring_segment_base_address);
        const auto segment_end   = segment_begin + entry[0].bits.ring_segment_size;

        auto p = read_dequeue_pointer() + 1;
        if(p == segment_end) {
            p         = segment_begin;
            cycle_bit = !cycle_bit;
        }
        write_dequeue_pointer(p);
    }
};
} // namespace usb::xhci
