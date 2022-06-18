#pragma once
#include <array>
#include <cstddef>

#include "asmcode.h"

constexpr auto page_direcotry_count = 64;

inline auto setup_identity_page_table() -> void {
    constexpr auto page_size_4k = size_t(4096);
    constexpr auto page_size_2m = 512 * page_size_4k;
    constexpr auto page_size_1g = 512 * page_size_2m;

    alignas(page_size_4k) static std::array<uint64_t, 512>                                   pml4_table;
    alignas(page_size_4k) static std::array<uint64_t, 512>                                   pdp_table;
    alignas(page_size_4k) static std::array<std::array<uint64_t, 512>, page_direcotry_count> page_directory;

    pml4_table[0] = reinterpret_cast<uint64_t>(&pdp_table[0]) | 0x003;
    for(auto i_pdpt = 0; i_pdpt < page_directory.size(); i_pdpt += 1) {
        pdp_table[i_pdpt] = reinterpret_cast<uint64_t>(&page_directory[i_pdpt]) | 0x003;
        for(auto i_pd = 0; i_pd < 512; i_pd += 1) {
            page_directory[i_pdpt][i_pd] = i_pdpt * page_size_1g + i_pd * page_size_2m | 0x083;
        }
    }

    set_cr3(reinterpret_cast<uint64_t>(&pml4_table[0]));
}
