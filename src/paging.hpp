#pragma once
#include <array>
#include <cstddef>

#include "asmcode.h"

namespace paging {
// page-map level 4
struct PML4EntryDirectory {
    uint64_t present : 1; // = 1
    uint64_t write : 1;
    uint64_t user : 1;
    uint64_t write_through : 1;
    uint64_t cache_disable : 1;
    uint64_t accessed : 1;
    uint64_t ignored1 : 1;
    uint64_t reserved1 : 1;
    uint64_t ignored2 : 4;

    uint64_t addr : 40;

    uint64_t ignored3 : 11;
    uint64_t execution_disable : 1;
} __attribute__((packed));

union PML4Entry {
    uint64_t           data = 0;
    PML4EntryDirectory directory;
};

// page directory pointer table
struct PDPTEntry1GBPage {
    uint64_t present : 1; // = 1
    uint64_t write : 1;
    uint64_t user : 1;
    uint64_t write_through : 1;
    uint64_t cache_disable : 1;
    uint64_t accessed : 1;
    uint64_t dirty : 1;
    uint64_t page_size : 1; // = 1
    uint64_t global : 1;
    uint64_t ignored2 : 3;

    uint64_t page_attribute_table : 1;
    uint64_t reserved2 : 17;
    uint64_t addr : 22;

    uint64_t ignored3 : 7;
    uint64_t protection_key : 4;
    uint64_t execution_disable : 1;
} __attribute__((packed));

struct PDPTEntryDirectory {
    uint64_t present : 1; // = 1
    uint64_t write : 1;
    uint64_t user : 1;
    uint64_t write_through : 1;
    uint64_t cache_disable : 1;
    uint64_t accessed : 1;
    uint64_t ignored1 : 1;
    uint64_t page_size : 1; // = 0
    uint64_t ignored2 : 4;

    uint64_t addr : 40;

    uint64_t ignored3 : 11;
    uint64_t execution_disable : 1;
} __attribute__((packed));

union PDPTEntry {
    uint64_t           data = 0;
    PDPTEntry1GBPage   frame;
    PDPTEntryDirectory directory;
};

// page directory
struct PDEntry2MB {
    uint64_t present : 1; // = 1
    uint64_t write : 1;
    uint64_t user : 1;
    uint64_t write_through : 1;
    uint64_t cache_disable : 1;
    uint64_t accessed : 1;
    uint64_t dirty : 1;
    uint64_t page_size : 1; // = 1
    uint64_t global : 1;
    uint64_t ignored2 : 3;

    uint64_t page_attribute_table : 1;
    uint64_t reserved2 : 8;
    uint64_t addr : 31;

    uint64_t ignored3 : 7;
    uint64_t protection_key : 4;
    uint64_t execution_disable : 1;
} __attribute__((packed));

struct PDEntryDirectory {
    uint64_t present : 1; // = 1
    uint64_t write : 1;
    uint64_t user : 1;
    uint64_t write_through : 1;
    uint64_t cache_disable : 1;
    uint64_t accessed : 1;
    uint64_t ignored1 : 1;
    uint64_t page_size : 1; // = 0
    uint64_t ignored2 : 4;

    uint64_t addr : 40;

    uint64_t ignored3 : 11;
    uint64_t execution_disable : 1;
} __attribute__((packed));

union PDEntry {
    uint64_t         data = 0;
    PDEntry2MB       frame;
    PDEntryDirectory directory;
};

// page table
struct PTEntry4KB {
    uint64_t present : 1; // = 1
    uint64_t write : 1;
    uint64_t user : 1;
    uint64_t write_through : 1;
    uint64_t cache_disable : 1;
    uint64_t accessed : 1;
    uint64_t dirty : 1;
    uint64_t page_attribute_table : 1;
    uint64_t global : 1;
    uint64_t ignored2 : 3;

    uint64_t addr : 40;

    uint64_t ignored3 : 7;
    uint64_t protection_key : 4;
    uint64_t execution_disable : 1;
} __attribute__((packed));

union PTEntry {
    uint64_t   data = 0;
    PTEntry4KB frame;
};

constexpr auto page_direcotry_count = 64;

inline auto setup_identity_page_table() -> void {
    using PageDirectory = std::array<PDEntry, 512>;

    constexpr auto page_size_4k = size_t(4096);
    constexpr auto page_size_2m = 512 * page_size_4k;
    constexpr auto page_size_1g = 512 * page_size_2m;

    alignas(page_size_4k) static std::array<PML4Entry, 512>                      pml4_table;
    alignas(page_size_4k) static std::array<PDPTEntry, 512>                      pdp_table;
    alignas(page_size_4k) static std::array<PageDirectory, page_direcotry_count> page_directory;

    pml4_table[0].data              = reinterpret_cast<uint64_t>(pdp_table.data());
    pml4_table[0].directory.present = 1;
    pml4_table[0].directory.write   = 1;

    for(auto i_pdpt = 0; i_pdpt < page_directory.size(); i_pdpt += 1) {
        pdp_table[i_pdpt].data              = reinterpret_cast<uint64_t>(&page_directory[i_pdpt]);
        pdp_table[i_pdpt].directory.present = 1;
        pdp_table[i_pdpt].directory.write   = 1;
        for(auto i_pd = 0; i_pd < 512; i_pd += 1) {
            auto& pde           = page_directory[i_pdpt][i_pd];
            pde.data            = i_pdpt * page_size_1g + i_pd * page_size_2m;
            pde.frame.present   = 1;
            pde.frame.write     = 1;
            pde.frame.page_size = 1;
        }
    }

    set_cr3(reinterpret_cast<uint64_t>(pml4_table.data()));
}
} // namespace paging
