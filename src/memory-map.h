#pragma once
#include <stdint.h>

struct MemoryMap {
    void*              buffer;
    unsigned long long buffer_size;
    unsigned long long map_size;
    unsigned long long map_key;
    unsigned long long descriptor_size;
    uint32_t           descriptor_version;
};

struct MemoryDescriptor {
    uint32_t  type;
    uintptr_t physical_start;
    uintptr_t virtual_start;
    uint64_t  number_of_pages;
    uint64_t  attribute;
};

#ifdef __cplusplus
enum class MemoryType {
    EfiReservedMemoryType = 0,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType,
};

inline auto operator==(const uint32_t lhs, const MemoryType rhs) -> bool {
    return lhs == static_cast<uint32_t>(rhs);
}

inline auto operator==(const MemoryType lhs, const uint32_t rhs) -> bool {
    return rhs == lhs;
}

inline auto is_available_memory_type(const MemoryType memory_type) -> bool {
    return memory_type == MemoryType::EfiBootServicesCode ||
           memory_type == MemoryType::EfiBootServicesData ||
           memory_type == MemoryType::EfiConventionalMemory;
}

inline auto uefi_page_size = 4096;
#endif
