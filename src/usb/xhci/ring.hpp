#pragma once
#include "../../error.hpp"
#include "registers.hpp"
#include "trb.hpp"

namespace usb::xhci {
class Ring {
  private:
    std::unique_ptr<TRB> buffer;
    size_t               buffer_count = 0;
    size_t               write_index;
    bool                 cycle_bit;

    auto copy_to_last(const std::array<uint32_t, 4>& data) -> void {
        // data[0..2] must be written prior to data[3].
        for(auto i = 0; i < 3; i += 1) {
            buffer.get()[write_index].data[i] = data[i];
        }
        buffer.get()[write_index].data[3] = (data[3] & 0xFFFF'FFFEu) | static_cast<uint32_t>(cycle_bit);
    }

    auto push(const std::array<uint32_t, 4>& data) -> TRB* {
        const auto trb_pointer = &buffer.get()[write_index];
        copy_to_last(data);
        write_index += 1;
        if(write_index == buffer_count - 1) {
            auto link              = LinkTRB(buffer.get());
            link.bits.toggle_cycle = true;
            copy_to_last(link.data);
            write_index = 0;
            cycle_bit   = !cycle_bit;
        }
        return trb_pointer;
    }

  public:
    auto initialize(const size_t buffer_count) -> Error {
        this->cycle_bit    = true;
        this->write_index  = 0;
        this->buffer_count = buffer_count;
        buffer.reset(new(std::align_val_t{64}, std::nothrow) TRB[buffer_count]);
        if(!buffer) {
            return Error::Code::NoEnoughMemory;
        }
        memset(buffer.get(), 0, buffer_count * sizeof(TRB));
        return Success();
    }

    template <typename T>
    auto push(const T& trb) -> TRB* {
        return push(trb.data);
    }

    auto get_buffer() const -> TRB* {
        return buffer.get();
    }

    Ring& operator=(const Ring&) = delete;

    Ring(const Ring&) = delete;

    Ring() = default;
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
    std::unique_ptr<TRB> buffer;
    size_t               buffer_count;

    std::unique_ptr<EventRingSegmentTableEntry> entry;
    InterrupterRegisterSet*                     interrupter;
    bool                                        cycle_bit;

  public:
    auto initialize(const size_t buffer_count, InterrupterRegisterSet* const interrupter) -> Error {
        cycle_bit          = true;
        this->buffer_count = buffer_count;
        this->interrupter  = interrupter;

        buffer.reset(new(std::align_val_t{64}, std::nothrow) TRB[buffer_count]);
        if(buffer == nullptr) {
            return Error::Code::NoEnoughMemory;
        }
        memset(buffer.get(), 0, buffer_count * sizeof(TRB));

        entry.reset(new(std::align_val_t{64}, std::nothrow) EventRingSegmentTableEntry[1]);
        if(entry == nullptr) {
            buffer.reset();
            return Error::Code::NoEnoughMemory;
        }
        memset(entry.get(), 0, 1 * sizeof(EventRingSegmentTableEntry));

        entry.get()[0].bits.ring_segment_base_address = std::bit_cast<uint64_t>(buffer.get());
        entry.get()[0].bits.ring_segment_size         = buffer_count;

        auto erstsz = interrupter->erstsz.read();
        erstsz.set_size(1);
        interrupter->erstsz.write(erstsz);

        write_dequeue_pointer(&buffer.get()[0]);

        auto erstba = interrupter->erstba.read();
        erstba.set_pointer(std::bit_cast<uint64_t>(entry.get()));
        interrupter->erstba.write(erstba);

        return Success();
    }

    auto read_dequeue_pointer() const -> TRB* {
        return std::bit_cast<TRB*>(interrupter->erdp.read().get_pointer());
    }

    auto write_dequeue_pointer(TRB* const p) -> void {
        auto erdp = interrupter->erdp.read();
        erdp.set_pointer(std::bit_cast<uint64_t>(p));
        interrupter->erdp.write(erdp);
    }

    auto has_front() const -> bool {
        return get_front()->bits.cycle_bit == cycle_bit;
    }

    auto get_front() const -> TRB* {
        return read_dequeue_pointer();
    }

    auto pop() -> void {
        const auto segment_begin = reinterpret_cast<TRB*>(entry.get()[0].bits.ring_segment_base_address);
        const auto segment_end   = segment_begin + entry.get()[0].bits.ring_segment_size;

        auto p = read_dequeue_pointer() + 1;
        if(p == segment_end) {
            p         = segment_begin;
            cycle_bit = !cycle_bit;
        }
        write_dequeue_pointer(p);
    }
};
} // namespace usb::xhci
