#pragma once
#include <array>

namespace usb {
namespace internal {
template <class T>
auto ceil(const T value, const unsigned int alignment) -> T {
    return (value + alignment - 1) & ~static_cast<T>(alignment - 1);
}

template <class T, class U>
auto mask_bits(T value, U mask) -> T {
    return value & ~static_cast<T>(mask - 1);
}

constexpr auto memory_pool_size = 1024 * 128;
alignas(64) inline uint8_t memory_pool[memory_pool_size];
inline uintptr_t memory_pointer = reinterpret_cast<uintptr_t>(memory_pool);
} // namespace internal

template <class T>
auto allocate_memory(const size_t size, const unsigned int alignment, const unsigned int boundary) -> T* {
    if(alignment > 0) {
        internal::memory_pointer = internal::ceil(internal::memory_pointer, alignment);
    }
    if(boundary > 0) {
        const auto next_boundary = internal::ceil(internal::memory_pointer, boundary);
        if(next_boundary < internal::memory_pointer + size) {
            internal::memory_pointer = next_boundary;
        }
    }

    if(reinterpret_cast<uintptr_t>(internal::memory_pool) + internal::memory_pool_size < internal::memory_pointer + size) {
        return nullptr;
    }

    const auto r = internal::memory_pointer;
    internal::memory_pointer += size;
    return reinterpret_cast<T*>(r);
}

template <class T>
auto allocate_array(const size_t num, const unsigned int alignment, const unsigned int boundary) -> T* {
    return allocate_memory<T>(sizeof(T) * num, alignment, boundary);
}

template <class T>
auto deallocate_memory(T* const pointer) -> void {
    // TODO
}

template <class T, unsigned int alignment = 64, unsigned int boundary = 4096>
class Allocator {
  public:
    auto allocate(const size_t n) -> T* {
        return allocate_array<T>(n, alignment, boundary);
    }

    auto deallocate(T* const pointer, [[maybe_unused]] const size_t num) -> void {
        deallocate_memory(pointer);
    }

    Allocator() noexcept                 = default;
    Allocator(const Allocator&) noexcept = default;
    template <class U>
    Allocator(const Allocator<U>&) noexcept {}
    ~Allocator() noexcept                  = default;
    Allocator& operator=(const Allocator&) = default;
};
} // namespace usb
