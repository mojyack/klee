#pragma once
#include <array>
#include <unordered_map>

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

constexpr auto bytes_per_page = 4096;

using PageTable = std::array<PTEntry, 512>;

struct PageDirectory {
    alignas(4096) std::array<PDEntry, 512> data;
    std::array<std::unique_ptr<PageTable*>, 512> resource = {nullptr};
};

struct PageDirectoryPointerTable {
    alignas(4096) std::array<PDPTEntry, 512> data;
    std::array<std::unique_ptr<PageDirectory*>, 512> resource = {nullptr};
};

struct PageMapLevel4Table {
    alignas(4096) std::array<PML4Entry, 512> data;
    std::array<std::unique_ptr<PageDirectoryPointerTable*>, 512> resource;
};

inline auto setup_identity_page_table() -> void {
    constexpr auto page_size_4k = size_t(4096);
    constexpr auto page_size_2m = 512 * page_size_4k;
    constexpr auto page_size_1g = 512 * page_size_2m;

    using PageDirectory                               = std::array<PDEntry, 512>;
    alignas(4096) static auto page_directory_resource = std::array<PageDirectory, 64>();
    alignas(4096) static auto pdp_table               = std::array<PDPTEntry, 512>();
    alignas(4096) static auto pml4_table              = std::array<PML4Entry, 512>();

    pml4_table[0].data              = reinterpret_cast<uint64_t>(pdp_table.data());
    pml4_table[0].directory.present = 1;
    pml4_table[0].directory.write   = 1;

    for(auto i_pdpt = 0; i_pdpt < page_directory_resource.size(); i_pdpt += 1) {
        auto& pd   = page_directory_resource[i_pdpt];
        auto& pdpe = pdp_table[i_pdpt];

        pdpe.data              = reinterpret_cast<uint64_t>(&pd);
        pdpe.directory.present = 1;
        pdpe.directory.write   = 1;

        for(auto i_pd = 0; i_pd < 512; i_pd += 1) {
            auto& pde = pd[i_pd];

            pde.data            = i_pdpt * page_size_1g + i_pd * page_size_2m;
            pde.frame.present   = 1;
            pde.frame.write     = 1;
            pde.frame.page_size = 1;
        }
    }

    set_cr3(reinterpret_cast<uint64_t>(pml4_table.data()));
}

inline auto split_addr_for_page_table(uintptr_t addr) -> std::array<uint16_t, 4> {
    auto r = std::array<uint16_t, 4>();

    addr = addr >> 11;
    for(auto i = 0; i < 4; i += 1) {
        r[i] = addr & 0x01FF;
        addr = addr >> 9;
    }
    return r;
}

inline auto map_virtual_to_physical(std::array<PML4Entry, 512>& pml4_table, const uintptr_t virtual_addr, const uintptr_t physical_addr) -> void {
    const auto [pml4, pdpt, pd, pt] = split_addr_for_page_table(virtual_addr);

    // auto& pml4e             = *reinterpret_cast<PML4Entry*>(&pml4_table[pml4]);
    // pml4e.directory.present = 1;
    // pml4e.directory.write   = 1;
    // pml4e.directory.addr    = ;
}
} // namespace paging
