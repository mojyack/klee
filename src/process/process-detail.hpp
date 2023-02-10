#pragma once
#include "../mutex.hpp"
#include "process.hpp"

namespace process {
struct ProcessDetail {
    Critical<paging::PageMapLevel4Table>              critical_pml4;
    Critical<std::vector<memory::SmartSingleFrameID>> critical_allocated_frames;
};

inline auto Process::get_pml4_address() -> paging::PageMapLevel4Table* {
    return &detail->critical_pml4.unsafe_access();
}

inline Process::Process(const uint64_t id) : id(id) {
    this->detail.reset(new ProcessDetail);

    // map kernel memory
    auto& pml4         = detail->critical_pml4.unsafe_access();
    auto [pml4e, pdpt] = pml4[0];
    pml4e.data         = &paging::get_identity_pdpt();
    pml4e.bits.present = 1;
    pml4e.bits.write   = 1;
    pml4e.bits.user    = 1;
}
} // namespace process
