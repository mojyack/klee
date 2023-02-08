#pragma once
#include <cstdint>
#include <memory>

#include "../log.hpp"
#include "../memory/allocator.hpp"
#include "pci.hpp"

namespace virtio::queue {
struct alignas(16) Descriptor {
    using Flags = uint16_t;

    static constexpr auto next     = 1 << 0; // mark a buffer as continuing via the next field
    static constexpr auto write    = 1 << 1; // mark a buffer as device write-only (otherwise device read-only)
    static constexpr auto indirect = 1 << 2; // the buffer contains a list of buffer descriptors

    uint64_t addr;
    uint32_t len;
    Flags    flags;
    uint16_t next_index;
} __attribute__((packed));

static_assert(sizeof(Descriptor) == 16);

struct alignas(2) AvailableRing {
    using Flags = uint16_t;

    static constexpr auto no_interrupt = 0x01;

    Flags    flags;
    uint16_t index;
    uint16_t ring[];
    // (if features::ring_index_event is negotiated)
    // uint16_t  used_event;

    static auto create(const uint32_t queue_size) -> AvailableRing* {
        const auto size = sizeof(AvailableRing) + sizeof(uint16_t) * queue_size;
        const auto ptr  = reinterpret_cast<AvailableRing*>(new uint8_t[size]);
        memset(ptr, 0, size);
        return ptr;
    }
} __attribute__((packed));

struct alignas(4) UsedRing {
    using Flags = uint16_t;

    static constexpr auto no_notify = 0x01;

    struct Elem {
        uint32_t id;
        uint32_t len;
    } __attribute__((packed));

    Flags    flags;
    uint16_t index;
    Elem     ring[];
    // (if features::ring_index_event is negotiated)
    // uint16_t  available_event;

    static auto create(const uint32_t queue_size) -> UsedRing* {
        const auto size = sizeof(UsedRing) + sizeof(Elem) * queue_size;
        const auto ptr  = reinterpret_cast<UsedRing*>(new uint8_t[size]);
        memset(ptr, 0, size);
        return ptr;
    }
} __attribute__((packed));

class Queue {
  private:
    std::unique_ptr<Descriptor>    descriptors;
    std::unique_ptr<AvailableRing> available_ring;
    std::unique_ptr<UsedRing>      used_ring;
    uint32_t                       added_descriptors = 0;
    uint32_t                       free_head         = 0;
    uint32_t                       last_used         = 0;
    uint16_t                       size;

    uint16_t                    queue_number;
    uint16_t*                   notify_address;
    volatile pci::CommonConfig* config;

    memory::SmartFrameID frame_id;

    auto barrier() -> void {
        asm volatile(""
                     :
                     :
                     : "memory");
    }

  public:
    template <size_t buffer_size_max>
    static constexpr auto buffer_size_check() -> void {
        static_assert(buffer_size_max <= memory::bytes_per_frame, "buffer size is larger than frame size");
    }

    auto get_next_descriptor_buffer(const size_t len) -> void* {
        auto& d = descriptors.get()[free_head % size];
        d.len   = len;
        free_head += 2; // free_head + 1 is for device response
        added_descriptors += 1;
        return reinterpret_cast<void*>(d.addr);
    }

    auto notify_device() -> void {
        auto& avail = *available_ring.get();

        for(auto i = 0; i < added_descriptors; i += 1) {
            const auto descriptor_index                             = (free_head - (added_descriptors - i) * 2) % size;
            avail.ring[avail.index % size]                          = descriptor_index;
            descriptors.get()[(descriptor_index) % size].flags      = Descriptor::next;
            descriptors.get()[(descriptor_index) % size].next_index = (descriptor_index + 1) % size;
            descriptors.get()[(descriptor_index + 1) % size].flags  = Descriptor::write;
            descriptors.get()[(descriptor_index + 1) % size].len    = memory::bytes_per_frame;
            avail.index += 1;
        }

        added_descriptors = 0;

        barrier();

        config->queue_select = queue_number;

        *reinterpret_cast<volatile uint16_t*>(notify_address) = avail.index;
    }

    auto read_one_buffer(void** const request, void** const result) -> bool {
        auto& used = *used_ring.get();
        if(last_used == used.index) {
            return false;
        }

        const auto e = used.ring[last_used % size];
        last_used += 1;
        *request = nullptr;
        *result  = nullptr;
        if(e.len == 0) {
            return true;
        } else if(e.len > memory::bytes_per_frame) {
            logger(LogLevel::Error, "chained descriptor is not supported");
            return true;
        } else {
            *request = reinterpret_cast<void*>(descriptors.get()[e.id].addr);
            for(auto i = e.id;;) {
                auto& d = descriptors.get()[i];
                if(d.flags & Descriptor::write) {
                    *result = reinterpret_cast<void*>(d.addr);
                    break;
                }
                if(d.flags & Descriptor::next) {
                    i = d.next_index;
                } else {
                    *result = nullptr;
                    break;
                }
            }
        }
        return true;
    }

    auto get_pointers() const -> std::tuple<const Descriptor*, const AvailableRing*, const UsedRing*> {
        return {descriptors.get(), available_ring.get(), used_ring.get()};
    }

    Queue()        = default;
    Queue(Queue&&) = default;
    Queue(const uint16_t queue_number, volatile pci::CommonConfig* config, uint16_t* const notify_address) : size(config->queue_size),
                                                                                                             queue_number(queue_number),
                                                                                                             notify_address(notify_address),
                                                                                                             config(config) {
        descriptors.reset(new queue::Descriptor[size]);
        available_ring.reset(AvailableRing::create(size));
        used_ring.reset(UsedRing::create(size));

        if(auto r = memory::allocate(size); !r) {
            logger(LogLevel::Error, "virtio: failed to allocate frames for virtio device: %d\n", r.as_error());
            return;
        } else {
            frame_id = std::move(r.as_value());
        }

        for(auto i = size_t(0); i < size; i += 1) {
            auto& d      = descriptors.get()[i];
            d.addr       = reinterpret_cast<uint64_t>((*frame_id + i).get_frame());
            d.len        = memory::bytes_per_frame;
            d.flags      = 0;
            d.next_index = (i + 1 != size) ? i + 1 : 0;
        }
        free_head = 0;

        auto& avail = *available_ring.get();
        avail.flags = 0;
        avail.index = 0;

        auto& used = *used_ring.get();
        used.flags = 0;
        used.index = 0;
    }
};

inline auto set_queue_to_config(volatile pci::CommonConfig& config, const uint16_t queue_number, queue::Queue& queue, std::optional<uint32_t> msix_entry_number) -> void {
    constexpr auto no_vector = 0xFFFF;

    const auto [descriptors, avail, used] = queue.get_pointers();

    config.queue_select = queue_number;
    config.queue_desc   = reinterpret_cast<uint64_t>(descriptors);
    config.queue_driver = reinterpret_cast<uint64_t>(avail);
    config.queue_device = reinterpret_cast<uint64_t>(used);
    if(msix_entry_number) {
        config.config_msix_vector = no_vector;
        config.queue_msix_vector  = *msix_entry_number;
    }
    config.queue_enable = 1;
}

} // namespace virtio::queue
