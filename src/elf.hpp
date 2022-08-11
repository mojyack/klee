#pragma once
#include <cstring>
#include <limits>

#include "memory-manager.hpp"

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
    if(!(c)) {          \
        return e;    \
    }

inline auto load_elf(SmartFrameID& image) -> Result<void*> {
    const auto bytes_limit = image.get_frames() * bytes_per_frame;

    auto& elf = *reinterpret_cast<ELF*>(image->get_frame());
    assert(memcmp(elf.magic, "\177ELF", 4) == 0, Error::Code::NotELF);
    assert(elf.program_header_size >= sizeof(ProgramHeader), Error::Code::InvalidELF);
    assert(elf.program_header_address + elf.program_header_size * elf.program_header_limit <= bytes_limit, Error::Code::InvalidELF);

    const auto program_headers = static_cast<uint8_t*>(image->get_frame()) + elf.program_header_address;
    auto       segment_first   = std::numeric_limits<uint64_t>::max();
    auto       segment_last    = size_t(0);

    for(auto i = 0; i < elf.program_header_limit; i += 1) {
        const auto& ph = *reinterpret_cast<ProgramHeader*>(program_headers + elf.program_header_size * i);
        if(ph.type != 0x01) {
            // not a loadable segment
            continue;
        }
        printk("segment found: %X %X +%X\n", ph.offset, ph.p_address, ph.memsize);
        segment_first = std::min(segment_first, ph.p_address);
        segment_last  = std::max(segment_last, ph.p_address + ph.memsize);
    }
    return reinterpret_cast<void*>(static_cast<uint8_t*>(image->get_frame()) + 0x120/*elf.entry_address*/);

    /*
    struct ELF*           elf             = (struct ELF*)(file_load_addr);
    struct ProgramHeader* program_headers = (struct ProgramHeader*)(file_load_addr + elf->program_header_address);
    {
        EFI_PHYSICAL_ADDRESS first = MAX_UINT64;
        EFI_PHYSICAL_ADDRESS last  = 0;
        for(struct ProgramHeader* program_header = program_headers; program_header < program_headers + elf->program_header_limit; program_header += 1) {
            if(program_header->type != 0x01) {
                // not a loadable segment
                continue;
            }
            Print(L"[elf] program_header: offset 0x%0lx, address 0x%0lx, filesize 0x%0lx, memsize 0x%0lx\n", program_header->offset, program_header->p_address, program_header->filesize, program_header->memsize);
            first = MIN(first, program_header->p_address);
            last  = MAX(last, program_header->p_address + program_header->memsize);
        }
        assert(allocate_address(first, last - first), L"failed to allocate pages for program segment");
        for(struct ProgramHeader* program_header = program_headers; program_header < program_headers + elf->program_header_limit; program_header += 1) {
            if(program_header->type != 0x01) {
                // not a loadable segment
                continue;
            }
            EFI_PHYSICAL_ADDRESS address  = program_header->p_address;
            EFI_PHYSICAL_ADDRESS offset   = program_header->offset;
            UINT64               filesize = program_header->filesize;
            UINT64               memsize  = program_header->memsize;
            UINT64               padding  = memsize - filesize;
            CopyMem((VOID*)address, (VOID*)(file_load_addr + offset), filesize);
            SetMem((VOID*)(address + filesize), padding, 0);
        }
    }
    *entry = elf->entry_address;
    assert(free_pool((VOID*)file_load_addr), L"failed to free pool");
    return EFI_SUCCESS;
    */
}

#undef assert
} // namespace elf
