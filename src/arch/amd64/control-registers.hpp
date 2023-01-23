#pragma once
#include <cstdint>

namespace amd64::cr {
union CR0 {
    uint64_t data;
    struct {
        uint64_t protected_mode_enable : 1;
        uint64_t monitor_coprocessor : 1;
        uint64_t emulation : 1;
        uint64_t task_switched : 1;
        uint64_t extension_type : 1;
        uint64_t numeric_error : 1;
        uint64_t reserved1 : 10;
        uint64_t write_protect : 1;
        uint64_t reserved2 : 1;
        uint64_t alignment_mask : 1;
        uint64_t reserved3 : 10;
        uint64_t not_write_through : 1;
        uint64_t cache_disable : 1;
        uint64_t paging : 1;
        uint64_t reserved4 : 32;
    } __attribute__((packed)) bits;

    static auto load() -> CR0 {
        auto cr0 = CR0();
        __asm__(
            "mov %0, cr0;"
            : "=r"(cr0.data));
        return cr0;
    }

    auto apply() const -> void {
        __asm__(
            "mov cr0, %0;"
            :
            : "r"(data));
    }
};

union CR3 {
    uint64_t data;
    struct {
        uint64_t pml4_table_address : 64;
    } __attribute__((packed)) bits;

    static auto load() -> CR3 {
        auto cr3 = CR3();
        __asm__(
            "mov %0, cr3;"
            : "=r"(cr3.data));
        return cr3;
    }

    auto apply() const -> void {
        __asm__(
            "mov cr3, %0;"
            :
            : "r"(data));
    }
};
} // namespace amd64::cr
