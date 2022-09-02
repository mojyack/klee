#pragma once
#include <vector>

#include "io.hpp"
#include "log.hpp"

namespace acpi {
namespace internal {
template <class T>
auto sum_bytes(const T* const data, const size_t bytes) -> uint8_t {
    return sum_bytes(reinterpret_cast<const uint8_t*>(data), bytes);
}

template <>
inline auto sum_bytes<uint8_t>(const uint8_t* const data, const size_t bytes) -> uint8_t {
    auto r = uint8_t(0);
    for(auto i = size_t(0); i < bytes; i += 1) {
        r += data[i];
    }
    return r;
}
} // namespace internal

struct RSDP {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    char     reserved[3];

    auto is_valid() const -> bool {
        if(std::strncmp(signature, "RSD PTR ", 8) != 0) {
            logger(LogLevel::Error, "invalid signature %.8s\n", signature);
            return false;
        }
        if(revision != 2) {
            logger(LogLevel::Error, "ACPI revision is not 2 (%d)\n", revision);
            return false;
        }
        if(const auto sum = internal::sum_bytes(this, 20); sum != 0) {
            logger(LogLevel::Error, "checksum stage 1 not matched (%d != 0)\n", sum);
            return false;
        }
        if(const auto sum = internal::sum_bytes(this, 36); sum != 0) {
            logger(LogLevel::Error, "checksum stage 2 not matched (%d != 0)\n", sum);
            return false;
        }
        return true;
    }
} __attribute__((packed));

struct DescriptionHeader {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;

    auto is_valid(const char* const expected_signature) const -> bool {
        if(strncmp(signature, expected_signature, 4) != 0) {
            return false;
        }
        if(const auto sum = internal::sum_bytes(this, length); sum != 0) {
            return false;
        }
        return true;
    }
} __attribute__((packed));

struct XSDT {
    DescriptionHeader  header;
    DescriptionHeader* entries[];

    auto get_count() const -> size_t {
        return (header.length - sizeof(XSDT)) / sizeof(void*);
    }
} __attribute__((packed));

struct FADT {
    DescriptionHeader header;

    char     reserved1[76 - sizeof(header)];
    uint32_t pm_tmr_blk;
    char     reserved2[112 - 80];
    uint32_t flags;
    char     reserved3[276 - 116];
} __attribute__((packed));

struct MADT {
    struct Entry {
        enum Type : uint8_t {
            LAPIC                            = 0,
            IOAPIC                           = 1,
            IOAPICInterruptSourceOverride    = 2,
            IOAPICNonMaskableInterruptSource = 3,
            LAPICNonMaskableInterruptSource  = 4,
            LAPICAddressOverride             = 5,
            ProcessorLx2APIC                 = 9,
        };

        struct LAPICValue {
            uint8_t processor_id;
            uint8_t apic_id;
            union {
                uint32_t data;
                struct {
                    uint32_t enabled : 1;
                    uint32_t online_capable : 1;
                } bits;
            } __attribute__((packed)) flags;
        } __attribute__((packed));

        struct IOAPICValue {
            uint8_t  io_apic_id;
            uint8_t  reserved;
            uint32_t io_apic_address;
            uint32_t global_system_interrupt_base;
        } __attribute__((packed));

        struct IOAPICInterruptSourceOverrideValue {
            uint8_t  bus_source;
            uint8_t  irq_source;
            uint32_t global_system_interrupt;
            uint16_t flags;
        } __attribute__((packed));

        struct IOAPICNonMaskableInterruptSourceValue {
            uint8_t  nmi_source;
            uint8_t  reserved;
            uint16_t flags;
            uint32_t global_system_interrupt;
        } __attribute__((packed));

        struct LAPICNonMaskableInterruptSourceValue {
            uint8_t  acpi_processor_id;
            uint16_t flags;
            uint8_t  lint;
        } __attribute__((packed));

        struct LAPICAddressOverrideValue {
            uint16_t reserved;
            uint64_t lapic_address;
        } __attribute__((packed));

        struct ProcessorLx2APICValue {
            uint16_t reserved;
            uint32_t lx2apic_id;
            uint32_t flags;
            uint32_t apic_id;
        } __attribute__((packed));

        Type    type;
        uint8_t length;
        uint8_t value; // struct xValue;
    } __attribute__((packed));

    DescriptionHeader header;

    uint32_t lapic_address;
    uint32_t flags;
    uint8_t  entries; // Entries[];
} __attribute__((packed));

inline auto fadt = (const FADT*)(nullptr);
inline auto madt = (const MADT*)(nullptr);

inline auto wait_miliseconds(const uint64_t ms) -> void {
    constexpr auto pm_timer_freq = 3579545;

    const auto pm_timer_32 = bool((fadt->flags >> 8) & 1);
    const auto start       = io_read32(fadt->pm_tmr_blk);
    auto       end         = start + pm_timer_freq * ms / 1000;
    if(!pm_timer_32) {
        end &= 0x00ffffffu;
    }

    if(end < start) { // overflow
        while(io_read32(fadt->pm_tmr_blk) >= start) {
            //
        }
    }
    while(io_read32(fadt->pm_tmr_blk) < end) {
        //
    }
}

inline auto initialize(RSDP& rsdp) -> bool {
    if(!rsdp.is_valid()) {
        logger(LogLevel::Error, "invalid RSDP");
        return false;
    }

    const auto& xsdt = *reinterpret_cast<const XSDT*>(rsdp.xsdt_address);
    if(!xsdt.header.is_valid("XSDT")) {
        logger(LogLevel::Error, "invalid XSDT");
        return false;
    }

    for(auto i = 0; i < xsdt.get_count(); i += 1) {
        const auto& e = *xsdt.entries[i];
        if(e.is_valid("FACP")) {
            fadt = reinterpret_cast<const FADT*>(&e);
        } else if(e.is_valid("APIC")) {
            madt = reinterpret_cast<const MADT*>(&e);
        }
    }

    if(fadt == nullptr) {
        logger(LogLevel::Error, "FADT not found\n");
        return false;
    }

    if(madt == nullptr) {
        logger(LogLevel::Error, "MADT not found\n");
        return false;
    }

    return true;
}

struct DetectCoreResult {
    std::vector<uint8_t> lapic_ids;
    uintptr_t            lapic_address;
    uintptr_t            ioapic_address = 0;
};

inline auto detect_cores() -> DetectCoreResult {
    auto r = DetectCoreResult{.lapic_address = madt->lapic_address};
    for(auto p = &madt->entries; p < reinterpret_cast<const uint8_t*>(madt) + madt->header.length; p += reinterpret_cast<const MADT::Entry*>(p)->length) {
        const auto& e = *reinterpret_cast<const MADT::Entry*>(p);
        switch(e.type) {
        case MADT::Entry::Type::LAPIC: {
            const auto& v = *reinterpret_cast<const MADT::Entry::LAPICValue*>(&e.value);
            if(v.flags.bits.enabled) {
                r.lapic_ids.emplace_back(v.apic_id);
            }
        } break;
        case MADT::Entry::Type::IOAPIC: {
            const auto& v    = *reinterpret_cast<const MADT::Entry::IOAPICValue*>(&e.value);
            r.ioapic_address = v.io_apic_address;
        } break;
        case MADT::Entry::Type::LAPICAddressOverride: {
            const auto& v    = *reinterpret_cast<const MADT::Entry::LAPICAddressOverrideValue*>(&e.value);
            r.ioapic_address = v.lapic_address;
        } break;
        default:
            break;
        }
    }
    return r;
}
} // namespace acpi
