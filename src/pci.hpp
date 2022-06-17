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
           (reg_addr & 0xFCu);
}
} // namespace internal

struct ClassCode {
    uint8_t base;
    uint8_t sub;
    uint8_t interface;

    auto match(const int8_t base) const -> bool {
        return this->base == base;
    }

    auto match(const int8_t base, const int8_t sub) const -> bool {
        return match(base) && this->sub == sub;
    }

    auto match(const int8_t base, const int8_t sub, const int8_t interface) const -> bool {
        return match(base, sub) && this->interface == interface;
    }
};

struct Device {
    uint8_t   bus;
    uint8_t   device;
    uint8_t   function;
    uint8_t   header_type;
    ClassCode class_code;
};

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

inline auto read_vender_id(const Device& dev) -> uint16_t {
    return read_vender_id(dev.bus, dev.device, dev.function);
}

inline auto read_device_id(const uint8_t bus, const uint8_t device, const uint8_t function) -> uint16_t {
    write_address(internal::make_address(bus, device, function, 0x00));
    return read_data() >> 16;
}

inline auto read_header_type(const uint8_t bus, const uint8_t device, const uint8_t function) -> uint8_t {
    write_address(internal::make_address(bus, device, function, 0x0c));
    return (read_data() >> 16) & 0xFFu;
}

inline auto read_class_code(const uint8_t bus, const uint8_t device, const uint8_t function) -> ClassCode {
    write_address(internal::make_address(bus, device, function, 0x08));
    const auto reg = read_data();
    return {
        static_cast<uint8_t>((reg >> 24) & 0xFFu),
        static_cast<uint8_t>((reg >> 16) & 0xFFu),
        static_cast<uint8_t>((reg >> 8) & 0xFFu),
    };
}

inline auto read_bus_numbers(const uint8_t bus, const uint8_t device, const uint8_t function) -> uint32_t {
    write_address(internal::make_address(bus, device, function, 0x18));
    return read_data();
}

inline auto is_single_function_device(uint8_t header_type) -> bool {
    return (header_type & 0x80u) == 0;
}

inline auto read_register(const Device& device, const uint8_t address) -> uint32_t {
    write_address(internal::make_address(device.bus, device.device, device.function, address));
    return read_data();
}

inline auto write_register(const Device& device, const uint8_t address, const uint32_t value) -> void {
    write_address(internal::make_address(device.bus, device.device, device.function, address));
    write_data(value);
}

inline auto calc_bar_address(const size_t bar_index) -> uint8_t {
    return 0x10 + 4 * bar_index;
}

inline auto read_bar(const Device& device, const size_t bar_index) -> Result<uint64_t> {
    if(bar_index >= 6) {
        return Error::Code::IndexOutOfRange;
    }

    const auto address = calc_bar_address(bar_index);
    const auto bar     = read_register(device, address);

    // 32 bit address
    if((bar & 0x04u) == 0) {
        return bar;
    }

    // 64 bit address
    if(bar_index >= 5) {
        return Error::Code::IndexOutOfRange;
    }

    const auto bar_upper = read_register(device, address + 4);
    return bar | static_cast<uint64_t>(bar_upper) << 32;
}

class Devices {
  private:
    std::array<Device, 32> data;
    size_t                 size;

    auto add_device(const Device& device) -> Error {
        if(size == data.size()) {
            return Error::Code::Full;
        }
        data[size] = device;
        size += 1;
        return Error::Code::Success;
    }

    auto scan_function(const uint8_t bus, const uint8_t dev, const uint8_t fn) -> Error {
        const auto header_type = read_header_type(bus, dev, fn);
        const auto class_code  = read_class_code(bus, dev, fn);
        const auto device      = Device{bus, dev, fn, header_type, class_code};

        if(const auto error = add_device(device)) {
            return error;
        }

        if(class_code.match(0x06u, 0x04u)) {
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
