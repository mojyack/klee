#pragma once
#include <Uefi.h>

struct ELF {
    UINT8  magic[4];
    UINT8  foramt;
    UINT8  endian;
    UINT8  elf_version_1;
    UINT8  osabi;
    UINT8  abi_version;
    UINT8  padding[7];
    UINT16 type;
    UINT16 machine;
    UINT32 elf_version_2;
    UINT64 entry_address;
    UINT64 program_header_address;
    UINT64 section_header_address;
    UINT32 flags;
    UINT16 elf_header_size;
    UINT16 program_header_size;
    UINT16 program_header_limit;
    UINT16 section_header_size;
    UINT16 section_header_limit;
    UINT16 section_header_string_number;
};

struct ProgramHeader {
    UINT32 type;
    UINT32 flags;
    UINT64 offset;
    UINT64 p_address;
    UINT64 v_address;
    UINT64 filesize;
    UINT64 memsize;
    UINT64 align;
};

EFI_STATUS load_elf(EFI_FILE_PROTOCOL* root, const CHAR16* path, EFI_PHYSICAL_ADDRESS* const entry);
