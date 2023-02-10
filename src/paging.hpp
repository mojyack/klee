#pragma once
#include <array>

#include "arch/amd64/control-registers.hpp"
#include "constants.hpp"

namespace paging {
// page table
union PTEntry {
    uint64_t data = 0;

    struct {
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
    } __attribute__((packed)) bits;
};

// page directory
union PDEntry {
    std::array<PTEntry, 512>* data = 0;
    uint64_t                  data_huge;

    struct {
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
    } __attribute__((packed)) bits_huge; // 2MiB page

    struct {
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
    } __attribute__((packed)) bits;
};

// page directory pointer table
union PDPTEntry {
    std::array<PDEntry, 512>* data = 0;
    uint64_t                  data_huge;

    struct {
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
    } __attribute__((packed)) bits_huge; // 1GiB page

    struct {
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
    } __attribute__((packed)) bits;
};

// page-map level 4
union PML4Entry {
    std::array<PDPTEntry, 512>* data = 0;

    struct {
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
    } __attribute__((packed)) bits;
};

constexpr auto bytes_per_page = size_t(0x1000);

struct PageTable {
    alignas(0x1000) std::array<PTEntry, 512> data;

    auto operator[](const uint16_t index) -> PTEntry& {
        return data[index];
    }
};

struct PageDirectory {
    alignas(0x1000) std::array<PDEntry, 512> data;
    std::array<std::unique_ptr<PageTable>, 512> resource;

    auto operator[](const uint16_t index) -> std::pair<PDEntry&, PageTable&> {
        if(!resource[index]) {
            const auto r = new PageTable;
            resource[index].reset(r);

            auto& e        = data[index];
            e.data         = &r->data;
            e.bits.present = 1;
            e.bits.write   = 1;
            e.bits.user    = 1;
        }

        return {data[index], *resource[index]};
    }
};

struct PageDirectoryPointerTable {
    alignas(0x1000) std::array<PDPTEntry, 512> data;
    std::array<std::unique_ptr<PageDirectory>, 512> resource;

    auto operator[](const uint16_t index) -> std::pair<PDPTEntry&, PageDirectory&> {
        if(!resource[index]) {
            const auto r = new PageDirectory;
            resource[index].reset(r);

            auto& e        = data[index];
            e.data         = &r->data;
            e.bits.present = 1;
            e.bits.write   = 1;
            e.bits.user    = 1;
        }

        return {data[index], *resource[index]};
    }
};

struct PageMapLevel4Table {
    alignas(0x1000) std::array<PML4Entry, 512> data;
    std::array<std::unique_ptr<PageDirectoryPointerTable>, 512> resource;

    auto operator[](const uint16_t index) -> std::pair<PML4Entry&, PageDirectoryPointerTable&> {
        if(!resource[index]) {
            const auto r = new PageDirectoryPointerTable;
            resource[index].reset(r);

            auto& e        = data[index];
            e.data         = &r->data;
            e.bits.present = 1;
            e.bits.write   = 1;
            e.bits.user    = 1;
        }

        return {data[index], *resource[index]};
    }
};

enum Attribute : int {
    User    = 0b001,
    Write   = 0b010,
    Execute = 0b100,

    UserWrite   = User | Write,
    UserExecute = User | Execute,
};

inline auto get_identity_pdpt() -> std::array<PDPTEntry, 512>& {
    struct StaticPageDirectory {
        alignas(0x1000) std::array<PDEntry, 512> data;
    };

    struct StaticPageDirectoryPointerTable {
        alignas(0x1000) std::array<PDPTEntry, 512> data;
        std::array<StaticPageDirectory, constants::supported_memory_limit> resource;
    };

    static auto pdpt = StaticPageDirectoryPointerTable();
    static auto init = true;

    if(!init) {
        return pdpt.data;
    }
    init = false;

    constexpr auto bytes_per_pt = bytes_per_page * 512;
    constexpr auto bytes_per_pd = bytes_per_pt * 512;
    // constexpr auto bytes_per_pdpt = bytes_per_pd * 512;
    // constexpr auto bytes_per_pml4 = bytes_per_pdpt * 512;

    for(auto pdpti = size_t(0); pdpti < pdpt.resource.size(); pdpti += 1) {
        auto& pdpte = pdpt.data[pdpti];
        auto& pds   = pdpt.resource[pdpti].data;
        for(auto pdi = size_t(0); pdi < pds.size(); pdi += 1) {
            auto& pde               = pds[pdi];
            pde.data_huge           = pdpti * bytes_per_pd + pdi * bytes_per_pt;
            pde.bits_huge.present   = 1;
            pde.bits_huge.write     = 1;
            pde.bits_huge.page_size = 1;
        }
        pdpte.data         = &pds;
        pdpte.bits.present = 1;
        pdpte.bits.write   = 1;
    }

    return pdpt.data;
}

inline auto apply_pml4_table(PageMapLevel4Table& pml4) {
    amd64::cr::CR3{std::bit_cast<uint64_t>(&pml4.data)}.apply();
}

inline auto split_addr_for_page_table(uintptr_t addr) -> std::array<uint16_t, 4> {
    auto r = std::array<uint16_t, 4>();

    addr = addr >> 12;
    for(auto i = 0; i < 4; i += 1) {
        r[i] = addr & 0x01FF;
        addr = addr >> 9;
    }
    return r;
}

inline auto invlpg(const uintptr_t addr) -> void {
    __asm__ volatile("invlpg [%0];"
                     :
                     : "r"(addr)
                     : "memory");
}

inline auto map_virtual_to_physical(PageMapLevel4Table& pml4, const uintptr_t virtual_addr, const uintptr_t physical_addr, const int attr) -> void {
    const auto [pti, pdi, pdpti, pml4i] = split_addr_for_page_table(virtual_addr);

    auto [pml4e, pdpt] = pml4[pml4i];
    auto [pdpte, pd]   = pdpt[pdpti];
    auto [pde, pt]     = pd[pdi];
    auto& pte          = pt[pti];

    pte.data         = physical_addr;
    pte.bits.present = 1;
    pte.bits.user    = (attr & Attribute::User) != 0;
    pte.bits.write   = (attr & Attribute::Write) != 0;

    invlpg(virtual_addr);
}
} // namespace paging
