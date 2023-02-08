#pragma once
#include "memory-map.h"

namespace memory {
enum class MemoryType : uint32_t {
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

constexpr auto operator==(const uint32_t lhs, const MemoryType rhs) -> bool {
    return lhs == static_cast<uint32_t>(rhs);
}

constexpr auto operator==(const MemoryType lhs, const uint32_t rhs) -> bool {
    return rhs == lhs;
}

constexpr auto is_available_memory_type(const MemoryType memory_type) -> bool {
    return memory_type == MemoryType::EfiBootServicesCode ||
           memory_type == MemoryType::EfiBootServicesData ||
           memory_type == MemoryType::EfiConventionalMemory;
}

constexpr auto uefi_page_size = 4096;
} // namespace memory
