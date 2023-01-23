#pragma once
#include <cstring>
#include <limits>
#include <vector>

#include "arch/amd64/control-registers.hpp"
#include "memory-manager.hpp"
#include "paging.hpp"
#include "process/manager.hpp"

namespace elf {
struct ELF {
    uint8_t  magic[4];
    uint8_t  foramt;
    uint8_t  endian;
    uint8_t  elf_version_1;
    uint8_t  osabi;
    uint8_t  abi_version;
    uint8_t  padding[7];
    uint16_t type;
    uint16_t machine;
    uint32_t elf_version_2;
    uint64_t entry_address;
    uint64_t program_header_address;
    uint64_t section_header_address;
    uint32_t flags;
    uint16_t elf_header_size;
    uint16_t program_header_size;
    uint16_t program_header_limit;
    uint16_t section_header_size;
    uint16_t section_header_limit;
    uint16_t section_header_string_number;
} __attribute__((packed));

struct ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t p_address;
    uint64_t v_address;
    uint64_t filesize;
    uint64_t memsize;
    uint64_t align;
} __attribute__((packed));

#define assert(c, e) \
    if(!(c)) {       \
        return e;    \
    }

struct LoadedELF {
    std::vector<SmartFrameID> allocated_frames;
    void*                     entry;
};

static_assert(paging::bytes_per_page == bytes_per_frame);

inline auto load_elf(SmartFrameID& image, paging::PageDirectoryPointerTable& pdpt, process::Process* const process, const process::AutoLock& lock) -> Result<LoadedELF> {
    const auto bytes_limit = image.get_frames() * bytes_per_frame;
    const auto image_addr  = reinterpret_cast<uint8_t*>(image->get_frame());

    auto& elf = *reinterpret_cast<ELF*>(image_addr);
    assert(memcmp(elf.magic, "\177ELF", 4) == 0, Error::Code::NotELF);
    assert(elf.program_header_size >= sizeof(ProgramHeader), Error::Code::InvalidELF);
    assert(elf.program_header_address + elf.program_header_size * elf.program_header_limit <= bytes_limit, Error::Code::InvalidELF);

    const auto program_headers = image_addr + elf.program_header_address;
    auto       segment_first   = std::numeric_limits<uint64_t>::max();
    auto       segment_last    = size_t(0);

    for(auto i = 0; i < elf.program_header_limit; i += 1) {
        const auto& ph = *reinterpret_cast<ProgramHeader*>(program_headers + elf.program_header_size * i);
        if(ph.type != 0x01) {
            // not a loadable segment
            continue;
        }
        segment_first = std::min(segment_first, ph.p_address);
        segment_last  = std::max(segment_last, ph.p_address + ph.memsize);
    }
    segment_first = segment_first & 0xFFFF'FFFF'FFFF'F000;

    const auto num_frames       = (segment_last - segment_first + bytes_per_frame - 1) / bytes_per_frame;
    auto       allocated_frames = std::vector<SmartFrameID>(num_frames);
    for(auto i = 0; i < allocated_frames.size(); i += 1) {
        auto& f = allocated_frames[i];

        if(auto r = allocator->allocate(1); !r) {
            return r.as_error();
        } else {
            f = std::move(r.as_value());
        }

        const auto physical_addr = reinterpret_cast<uint64_t>(f->get_frame());
        const auto virtual_addr  = segment_first + paging::bytes_per_page * i;
        paging::map_virtual_to_physical(&pdpt, virtual_addr, physical_addr, paging::Attribute::UserExecute);
    }

    process->apply_page_map(lock, process::manager->get_pml4_table());

    auto cr0               = amd64::cr::CR0::load();
    cr0.bits.write_protect = 0;
    cr0.apply();
    for(auto i = 0; i < elf.program_header_limit; i += 1) {
        const auto& ph = *reinterpret_cast<ProgramHeader*>(program_headers + elf.program_header_size * i);
        if(ph.type != 0x01) {
            continue;
        }
        const auto address  = reinterpret_cast<uint8_t*>(ph.p_address);
        const auto offset   = ph.offset;
        const auto filesize = ph.filesize;
        const auto memsize  = ph.memsize;
        const auto padding  = memsize - filesize;

        assert(offset + memsize <= bytes_limit, Error::Code::InvalidELF);
        memcpy(address, image_addr + offset, filesize);
        memset(address + filesize, 0, padding);
    }
    cr0.bits.write_protect = 1;
    cr0.apply();

    return LoadedELF{std::move(allocated_frames), reinterpret_cast<void*>(elf.entry_address)};
}

#undef assert
} // namespace elf
