#pragma once
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
            logger(LogLevel::Error, "invalid signature %.4s\n", signature);
            return false;
        }
        if(const auto sum = internal::sum_bytes(this, length); sum != 0) {
            logger(LogLevel::Error, "checksum not matched (%d != 0)\n", sum);
            return false;
        }
        return true;
    }
} __attribute__((packed));

struct XSDT {
    DescriptionHeader header;

    auto get_count() const -> size_t {
        return (header.length - sizeof(DescriptionHeader)) / sizeof(uint64_t);
    }

    auto operator[](const size_t i) const -> const DescriptionHeader& {
        const auto entries = reinterpret_cast<const uint64_t*>(&header + 1);
        return *reinterpret_cast<const DescriptionHeader*>(entries[i]);
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

inline auto fadt = (const FADT*)(nullptr);

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
        const auto& e = xsdt[i];
        if(e.is_valid("FACP")) {
            fadt = reinterpret_cast<const FADT*>(&e);
            break;
        }
    }

    if(fadt == nullptr) {
        logger(LogLevel::Error, "FADT not found\n");
        return false;
    }

    return true;
}
} // namespace acpi
