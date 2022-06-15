#pragma once
#include <cstdint>

#include "asmcode.h"
#include "error.hpp"

namespace pci {
namespace internal {
static constexpr auto config_address = 0x0CF8;
static constexpr auto config_data    = 0x0CFC;

static auto make_address(const uint8_t bus, const uint8_t device, const uint8_t function, const uint8_t reg_addr) -> uint32_t {
    return 1 << 31 | // enable bit
           bus << 16 |
           device << 11 |
           function << 8 |
           reg_addr < 0x0FCu;
}
} // namespace internal

inline auto write_address(const uint32_t address) -> void {
    io_set32(internal::config_address, address);
}

inline auto write_data(const uint32_t data) -> void {
    io_set32(internal::config_data, data);
}

inline auto read_data() -> uint32_t {
    return io_read32(internal::config_data);
}

inline auto read_vender_id(const uint8_t bus, const uint8_t device, const uint8_t function) -> uint16_t {
    write_address(internal::make_address(bus, device, function, 0x00));
    return read_data() & 0xFFFFu;
}

inline auto read_device_id(const uint8_t bus, const uint8_t device, const uint8_t function) -> uint16_t {
    write_address(internal::make_address(bus, device, function, 0x00));
    return read_data() >> 16;
}

inline auto read_header_type(const uint8_t bus, const uint8_t device, const uint8_t function) -> uint8_t {
    write_address(internal::make_address(bus, device, function, 0x0c));
    return (read_data() >> 16) & 0xFFu;
}

inline auto read_class_code(const uint8_t bus, const uint8_t device, const uint8_t function) -> uint32_t {
    write_address(internal::make_address(bus, device, function, 0x08));
    return read_data();
}

inline auto read_bus_numbers(const uint8_t bus, const uint8_t device, const uint8_t function) -> uint32_t {
    write_address(internal::make_address(bus, device, function, 0x18));
    return read_data();
}

inline auto is_single_function_device(uint8_t header_type) -> bool {
    return (header_type & 0x80u) == 0;
}

struct Device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t header_type;
};

class Devices {
  private:
    std::array<Device, 32> data;
    size_t                 size;

    auto add_device(const uint8_t bus, const uint8_t dev, const uint8_t fn, const uint8_t header_type) -> Error {
        if(size == data.size()) {
            return Error::Code::Full;
        }
        data[size] = Device{bus, dev, fn, header_type};
        size += 1;
        return Error::Code::Success;
    }

    auto scan_function(const uint8_t bus, const uint8_t dev, const uint8_t fn) -> Error {
        const auto header_type = read_header_type(bus, dev, fn);
        if(const auto error = add_device(bus, dev, fn, header_type)) {
            return error;
        }

        const auto class_code = read_class_code(bus, dev, fn);
        const auto base       = (class_code >> 24) & 0xFFu;
        const auto sub        = (class_code >> 16) & 0xFFu;

        if(base == 0x06u && sub == 0x04u) {
            // standard PCI-PCI bridge
            const auto bus_numbers   = read_bus_numbers(bus, dev, fn);
            const auto secondary_bus = (bus_numbers >> 8) & 0xFFu;
            return scan_bus(secondary_bus);
        }
        return Error::Code::Success;
    }

    auto scan_device(const uint8_t bus, const uint8_t dev) -> Error {
        if(const auto error = scan_function(bus, dev, 0)) {
            return error;
        }

        if(is_single_function_device(read_header_type(bus, dev, 0))) {
            return Error::Code::Success;
        }

        for(auto fn = uint8_t(1); fn < 8; fn += 1) {
            if(read_vender_id(bus, dev, fn) == 0xFFFFu) {
                continue;
            }
            if(const auto error = scan_function(bus, dev, fn)) {
                return error;
            }
        }

        return Error::Code::Success;
    }

    auto scan_bus(const uint8_t bus) -> Error {
        for(auto dev = uint8_t(0); dev < 32; dev += 1) {
            if(read_vender_id(bus, dev, 0) == 0xFFFFu) {
                continue;
            }
            if(const auto error = scan_device(bus, dev)) {
                return error;
            }
        }

        return Error::Code::Success;
    }

  public:
    auto scan_all_bus() -> Error {
        size = 0;

        if(is_single_function_device(read_header_type(0, 0, 0))) {
            return scan_bus(0);
        }

        for(auto fn = uint8_t(1); fn < 8; fn += 1) {
            if(read_vender_id(0, 0, fn) == 0xFFFFu) {
                continue;
            }
            if(const auto error = scan_bus(fn)) {
                return error;
            }
        }

        return Error::Code::Success;
    }

    auto get_devices() const -> std::pair<size_t, const decltype(data)&> {
        return {size, data};
    }
};
} // namespace pci
