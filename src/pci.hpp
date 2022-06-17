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

inline auto calc_bar_address(const size_t bar_index) -> uint8_t {
    return 0x10 + 4 * bar_index;
}

union CapabilityHeader {
    uint32_t data;
    struct {
        uint32_t cap_id : 8;
        uint32_t next_ptr : 8;
        uint32_t cap : 16;
    } __attribute__((packed)) bits;
} __attribute__((packed));

struct MSICapability {
    union {
        uint32_t data;
        struct {
            uint32_t cap_id : 8;
            uint32_t next_ptr : 8;
            uint32_t msi_enable : 1;
            uint32_t multi_msg_capable : 3;
            uint32_t multi_msg_enable : 3;
            uint32_t addr_64_capable : 1;
            uint32_t per_vector_mask_capable : 1;
            uint32_t : 7;
        } __attribute__((packed)) bits;
    } __attribute__((packed)) header;

    uint32_t msg_addr;
    uint32_t msg_upper_addr;
    uint32_t msg_data;
    uint32_t mask_bits;
    uint32_t pending_bits;
} __attribute__((packed));

struct MSIXCapability {
    union {
        uint32_t data;
        struct {
            uint32_t cap_id : 8;
            uint32_t next_ptr : 8;
            uint32_t table_limit : 11;
            uint32_t : 3;
            uint32_t mask : 1;
            uint32_t msix_enable : 1;
        } __attribute__((packed)) bits;
    } __attribute__((packed)) header;

    union {
        uint32_t data;
        struct {
            uint32_t bar_index : 3;
            uint32_t offset : 29;
        } __attribute__((packed)) bits;
    } __attribute__((packed)) table;

    union {
        uint32_t data;
        struct {
            uint32_t bar_index : 3;
            uint32_t offset : 29;
        } __attribute__((packed)) bits;
    } __attribute__((packed)) pba;
};

struct MSIXTable {
    uint32_t msg_addr;
    uint32_t msg_upper_addr;
    uint32_t msg_data;
    uint32_t vector;
} __attribute__((packed));

enum class MSITriggerMode {
    Edge  = 0,
    Level = 1,
};

enum class MSIDeliveryMode {
    Fixed          = 0b000,
    LowestPriority = 0b001,
    SMI            = 0b010,
    NMI            = 0b100,
    INIT           = 0b101,
    ExtINT         = 0b111,
};

struct Device {
  private:
    static constexpr auto capability_msi  = 0x05;
    static constexpr auto capability_msix = 0x11;

    auto read_msi_capability(const uint8_t cap_addr) const -> MSICapability {
        auto msi_cap = MSICapability();

        msi_cap.header.data = read_register(cap_addr);
        msi_cap.msg_addr    = read_register(cap_addr + 4);

        auto msg_data_addr = cap_addr + 8;
        if(msi_cap.header.bits.addr_64_capable) {
            msi_cap.msg_upper_addr = read_register(cap_addr + 8);
            msg_data_addr          = cap_addr + 12;
        }

        msi_cap.msg_data = read_register(msg_data_addr);

        if(msi_cap.header.bits.per_vector_mask_capable) {
            msi_cap.mask_bits    = read_register(msg_data_addr + 4);
            msi_cap.pending_bits = read_register(msg_data_addr + 8);
        }

        return msi_cap;
    }

    auto read_msix_capability(const uint8_t cap_addr) const -> MSIXCapability {
        auto msix_cap = MSIXCapability();

        msix_cap.header.data = read_register(cap_addr);
        msix_cap.table.data  = read_register(cap_addr + 4);
        msix_cap.pba.data    = read_register(cap_addr + 8);
        return msix_cap;
    }

    auto write_msi_capability(const uint8_t cap_addr, const MSICapability& msi_cap) const -> void {
        write_register(cap_addr, msi_cap.header.data);
        write_register(cap_addr + 4, msi_cap.msg_addr);

        auto msg_data_addr = cap_addr + 8;
        if(msi_cap.header.bits.addr_64_capable) {
            write_register(cap_addr + 8, msi_cap.msg_upper_addr);
            msg_data_addr = cap_addr + 12;
        }

        write_register(msg_data_addr, msi_cap.msg_data);

        if(msi_cap.header.bits.per_vector_mask_capable) {
            write_register(msg_data_addr + 4, msi_cap.mask_bits);
            write_register(msg_data_addr + 8, msi_cap.pending_bits);
        }
    }

    auto write_msix_capability(const uint8_t cap_addr, const MSIXCapability& msix_cap) const -> void {
        write_register(cap_addr, msix_cap.header.data);
    }

    auto configure_msi_register(const uint8_t cap_addr, const uint32_t msg_addr, const uint32_t msg_data, const unsigned int num_vector_exponent) const -> Error {
        auto msi_cap = read_msi_capability(cap_addr);
        if(msi_cap.header.bits.multi_msg_capable <= num_vector_exponent) {
            msi_cap.header.bits.multi_msg_enable = msi_cap.header.bits.multi_msg_capable;
        } else {
            msi_cap.header.bits.multi_msg_enable = num_vector_exponent;
        }

        msi_cap.header.bits.msi_enable = 1;
        msi_cap.msg_addr               = msg_addr;
        msi_cap.msg_data               = msg_data;

        write_msi_capability(cap_addr, msi_cap);
        return Error::Code::Success;
    }

    auto configure_msix_register(const uint8_t cap_addr, const uint32_t msg_addr, const uint32_t msg_data, const unsigned int num_vector_exponent) const -> Error {
        auto       msix_cap  = read_msix_capability(cap_addr);
        const auto table_bar = read_bar(msix_cap.table.bits.bar_index);
        const auto pba_bar   = read_bar(msix_cap.pba.bits.bar_index);
        if(!table_bar || !pba_bar) {
            return Error::Code::IndexOutOfRange;
        }

        const auto table_addr = (table_bar.as_value() & ~static_cast<uint64_t>(0x0F)) + (msix_cap.table.bits.offset << 3);
        // const auto pba_addr   = (pba_bar.as_value() & ~static_cast<uint64_t>(0x0F)) + (msix_cap.pba.bits.offset << 3);

        const auto table        = reinterpret_cast<MSIXTable*>(table_addr);
        table[0].msg_addr       = msg_addr;
        table[0].msg_upper_addr = 0;
        table[0].msg_data       = msg_data;
        table[0].vector         = 0;

        msix_cap.header.bits.msix_enable = 1;
        write_msix_capability(cap_addr, msix_cap);
        return Error::Code::Success;
    }

  public:
    uint8_t   bus;
    uint8_t   device;
    uint8_t   function;
    uint8_t   header_type;
    ClassCode class_code;

    auto read_vender_id() const -> uint16_t {
        return ::pci::read_vender_id(bus, device, function);
    }

    auto read_register(const uint8_t address) const -> uint32_t {
        write_address(internal::make_address(bus, device, function, address));
        return read_data();
    }

    auto write_register(const uint8_t address, const uint32_t value) const -> void {
        write_address(internal::make_address(bus, device, function, address));
        write_data(value);
    }

    auto read_bar(const size_t bar_index) const -> Result<uint64_t> {
        if(bar_index >= 6) {
            return Error::Code::IndexOutOfRange;
        }

        const auto address = calc_bar_address(bar_index);
        const auto bar     = read_register(address);

        // 32 bit address
        if((bar & 0x04u) == 0) {
            return bar;
        }

        // 64 bit address
        if(bar_index >= 5) {
            return Error::Code::IndexOutOfRange;
        }

        const auto bar_upper = read_register(address + 4);
        return bar | static_cast<uint64_t>(bar_upper) << 32;
    }

    auto read_capability_header(const uint8_t addr) const -> CapabilityHeader {
        auto header = CapabilityHeader();
        header.data = read_register(addr);
        return header;
    }

    auto configure_msi(const uint32_t msg_addr, uint32_t msg_data, const unsigned int num_vector_exponent) const -> Error {
        auto cap_addr      = read_register(0x34) & 0xFFu;
        auto msi_cap_addr  = 0;
        auto msix_cap_addr = 0;
        while(cap_addr != 0) {
            const auto header = read_capability_header(cap_addr);
            if(header.bits.cap_id == capability_msi) {
                msi_cap_addr = cap_addr;
            } else if(header.bits.cap_id == capability_msix) {
                msix_cap_addr = cap_addr;
            }
            cap_addr = header.bits.next_ptr;
        }

        if(msi_cap_addr) {
            return configure_msi_register(msi_cap_addr, msg_addr, msg_data, num_vector_exponent);
        } else if(msix_cap_addr) {
            return configure_msix_register(msix_cap_addr, msg_addr, msg_data, num_vector_exponent);
        }
        return Error::Code::NoPCIMSI;
    }

    auto configure_msi_fixed_destination(const uint8_t apic_id, const MSITriggerMode trigger_mode, const MSIDeliveryMode delivery_mode, const uint8_t vector, const unsigned int num_vector_exponent) const -> Error {
        const auto msg_addr = 0xFEE00000u | (apic_id << 12);
        auto       msg_data = (static_cast<uint32_t>(delivery_mode) << 8) | vector;
        if(trigger_mode == MSITriggerMode::Level) {
            msg_data |= 0xC000;
        }
        return configure_msi(msg_addr, msg_data, num_vector_exponent);
    }
};

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
