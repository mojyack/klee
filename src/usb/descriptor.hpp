#pragma once
#include <array>
#include <cstdint>

namespace usb {
struct DeviceDescriptor {
    static constexpr auto type = 1;

    uint8_t  length;             // offset 0
    uint8_t  descriptor_type;    // offset 1
    uint16_t usb_release;        // offset 2
    uint8_t  device_class;       // offset 4
    uint8_t  device_sub_class;   // offset 5
    uint8_t  device_protocol;    // offset 6
    uint8_t  max_packet_size;    // offset 7
    uint16_t vendor_id;          // offset 8
    uint16_t product_id;         // offset 10
    uint16_t device_release;     // offset 12
    uint8_t  manufacturer;       // offset 14
    uint8_t  product;            // offset 15
    uint8_t  serial_number;      // offset 16
    uint8_t  num_configurations; // offset 17
} __attribute__((packed));

struct ConfigurationDescriptor {
    static constexpr auto type = 2;

    uint8_t  length;              // offset 0
    uint8_t  descriptor_type;     // offset 1
    uint16_t total_length;        // offset 2
    uint8_t  num_interfaces;      // offset 4
    uint8_t  configuration_value; // offset 5
    uint8_t  configuration_id;    // offset 6
    uint8_t  attributes;          // offset 7
    uint8_t  max_power;           // offset 8
} __attribute__((packed));

struct InterfaceDescriptor {
    static constexpr auto type = 4;

    uint8_t length;              // offset 0
    uint8_t descriptor_type;     // offset 1
    uint8_t interface_number;    // offset 2
    uint8_t alternate_setting;   // offset 3
    uint8_t num_endpoints;       // offset 4
    uint8_t interface_class;     // offset 5
    uint8_t interface_sub_class; // offset 6
    uint8_t interface_protocol;  // offset 7
    uint8_t interface_id;        // offset 8
} __attribute__((packed));

struct EndpointDescriptor {
    static constexpr auto type = 5;

    uint8_t length;          // offset 0
    uint8_t descriptor_type; // offset 1
    union {
        uint8_t data;
        struct {
            uint8_t number : 4;
            uint8_t : 3;
            uint8_t dir_in : 1;
        } __attribute__((packed)) bits;
    } endpoint_address; // offset 2
    union {
        uint8_t data;
        struct {
            uint8_t transfer_type : 2;
            uint8_t sync_type : 2;
            uint8_t usage_type : 2;
            uint8_t : 2;
        } __attribute__((packed)) bits;
    } attributes;             // offset 3
    uint16_t max_packet_size; // offset 4
    uint8_t  interval;        // offset 6
} __attribute__((packed));

struct HIDDescriptor {
    static constexpr auto type = 33;

    uint8_t  length;          // offset 0
    uint8_t  descriptor_type; // offset 1
    uint16_t hid_release;     // offset 2
    uint8_t  country_code;    // offset 4
    uint8_t  num_descriptors; // offset 5

    struct ClassDescriptor {
        uint8_t  descriptor_type;
        uint16_t descriptor_length;
    } __attribute__((packed));

    auto get_class_descriptor(const size_t index) const -> ClassDescriptor* {
        if(index >= num_descriptors) {
            return nullptr;
        }
        const auto end_of_struct = reinterpret_cast<uintptr_t>(this) + sizeof(HIDDescriptor);
        return reinterpret_cast<ClassDescriptor*>(end_of_struct) + index;
    }
} __attribute__((packed));

template <class T>
auto descriptor_dynamic_cast(uint8_t* const data) -> T* {
    if(data[1] == T::type) {
        return reinterpret_cast<T*>(data);
    }
    return nullptr;
}

template <class T>
auto descriptor_dynamic_cast(const uint8_t* const data) -> const T* {
    if(data[1] == T::type) {
        return reinterpret_cast<const T*>(data);
    }
    return nullptr;
}
} // namespace usb
