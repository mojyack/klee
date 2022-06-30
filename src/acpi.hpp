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
        if(std::string_view(signature) != "RSD PTR ") {
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

inline auto initialize(RSDP& rsdp) -> void {
    if(!rsdp.is_valid()) {
        logger(LogLevel::Error, "invalid RSDP");
        return;
    }
    return;
}
} // namespace acpi
