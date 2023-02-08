#pragma once
#include <optional>

#include "../pci.hpp"
#include "flags.hpp"

namespace virtio::pci {
enum class ConfigType : uint32_t {
    Common = 1,
    Notify = 2,
    ISR    = 3,
    Device = 4,
    PCI    = 5,
};

struct Capability {
    union {
        uint32_t data;
        struct {
            uint32_t   cap_id : 8;
            uint32_t   next_ptr : 8;
            uint32_t   cap_len : 8;
            ConfigType config_type : 8;
        } __attribute__((packed)) bits;
    } __attribute__((packed)) header;

    union {
        uint32_t data;
        struct {
            uint32_t bar_index : 8;
            uint32_t reserved : 24;
        } __attribute__((packed)) bits;
    } __attribute__((packed)) body;

    uint32_t offset;
    uint32_t length;
} __attribute__((packed));

inline auto read_capability(const ::pci::Device& dev, const uint8_t addr) -> Capability {
    auto cap = Capability();

    cap.header.data = dev.read_register(addr);
    cap.body.data   = dev.read_register(addr + 4);
    cap.offset      = dev.read_register(addr + 8);
    cap.length      = dev.read_register(addr + 12);
    return cap;
};

struct NotificationCapability {
    Capability capability;
    uint32_t   notify_off_multiplier; /* Multiplier for queue_notify_off. */
} __attribute__((packed));

inline auto read_additional_notification_capability(const ::pci::Device& dev, const uint8_t addr) -> uint32_t {
    return dev.read_register(addr + 16);
}

struct CommonConfig {
    /* About the whole device. */
    uint32_t     device_feature_select; /* read-write */
    uint32_t     device_feature;        /* read-only for driver */
    uint32_t     driver_feature_select; /* read-write */
    uint32_t     driver_feature;        /* read-write */
    uint16_t     config_msix_vector;    /* read-write */
    uint16_t     num_queues;            /* read-only for driver */
    DeviceStatus device_status;         /* read-write */
    uint8_t      config_generation;     /* read-only for driver */

    /* About a specific virtqueue. */
    uint16_t queue_select;      /* read-write */
    uint16_t queue_size;        /* read-write */
    uint16_t queue_msix_vector; /* read-write */
    uint16_t queue_enable;      /* read-write */
    uint16_t queue_notify_off;  /* read-only for driver */
    uint64_t queue_desc;        /* read-write */
    uint64_t queue_driver;      /* read-write */
    uint64_t queue_device;      /* read-write */

    auto read_device_features() volatile -> Features {
        auto flags            = virtio::Features(0);
        device_feature_select = 0;
        flags |= static_cast<uint64_t>(device_feature);
        device_feature_select = 1;
        flags |= static_cast<uint64_t>(device_feature) << 32;
        return flags;
    }

    auto write_driver_features(const Features features) volatile -> void {
        driver_feature_select = 0;
        driver_feature        = features & 0xFFFFFFFF;
        driver_feature_select = 1;
        driver_feature        = features >> 32;
    }

    template <size_t len>
    auto get_queue_number() volatile -> std::array<uint16_t, len> {
        auto r = std::array<uint16_t, len>();
        auto i = uint16_t(0);
        for(auto& n : r) {
            auto found = false;
            for(; i < std::numeric_limits<uint16_t>::max(); i += 1) {
                queue_select    = i;
                const auto size = queue_size;
                if(size != 0) {
                    found = true;
                    n     = i;
                    break;
                }
            }
            if(!found) {
                return r;
            }
            i += 1;
        }
        return r;
    }

} __attribute__((packed));

inline auto get_config_address(const ::pci::Device& dev, const Capability& cap) -> void* {
    static_assert(sizeof(CommonConfig) % sizeof(uint32_t) == 0);
    const auto bar = dev.read_bar(cap.body.bits.bar_index);
    if(!bar) {
        return nullptr;
    }
    const auto base_addr = bar.as_value() & ~static_cast<uint64_t>(0x0F);
    return reinterpret_cast<void*>(base_addr + cap.offset);
}
} // namespace virtio::pci
