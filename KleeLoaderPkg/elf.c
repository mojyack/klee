#include <Guid/FileInfo.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleFileSystem.h>

#include "elf.h"
#include "memory.h"

#define assert(expr, message)                          \
    {                                                  \
        EFI_STATUS status = expr;                      \
        if(EFI_ERROR(status)) {                        \
            Print(L"[elf] %s: %r\n", message, status); \
            return status;                             \
        }                                              \
    }

EFI_STATUS load_elf(EFI_FILE_PROTOCOL* const root, const CHAR16* const path, EFI_PHYSICAL_ADDRESS* const entry) {
    EFI_FILE_PROTOCOL* file;
    assert(root->Open(root, &file, (CHAR16*)path, EFI_FILE_MODE_READ, 0), L"failed to open file");

    VOID* file_info_buffer;
    UINTN file_info_size = sizeof(EFI_FILE_INFO) + (StrLen(path) + 1) * sizeof(CHAR16);
    assert(allocate_pool(&file_info_buffer, file_info_size), L"failed to allocate memory for file info");
    assert(file->GetInfo(file, &gEfiFileInfoGuid, &file_info_size, file_info_buffer), L"failed to get file informations");
    EFI_FILE_INFO* file_info = (EFI_FILE_INFO*)file_info_buffer;
    UINTN          file_size = file_info->FileSize;
    assert(free_pool(file_info_buffer), L"failed to free pool");

    EFI_PHYSICAL_ADDRESS file_load_addr;
    assert(allocate_pool((VOID**)&file_load_addr, file_size), L"failed to allocate pool for loading");
    assert(file->Read(file, &file_size, (VOID*)file_load_addr), L"failed to read file");
    assert(file->Close(file), L"failed to close file");

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
}
