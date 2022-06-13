#include <Guid/FileInfo.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo2.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

#include "elf.h"
#include "memory.h"

struct MemoryMap {
    UINTN  buffer_size;
    VOID*  buffer;
    UINTN  map_size;
    UINTN  map_key;
    UINTN  descriptor_size;
    UINT32 descriptor_version;
};

EFI_STATUS get_memory_map(struct MemoryMap* const map) {
    if(map->buffer == NULL) {
        return EFI_BUFFER_TOO_SMALL;
    }
    map->map_size = map->buffer_size;
    return gBS->GetMemoryMap(
        &map->map_size,
        (EFI_MEMORY_DESCRIPTOR*)map->buffer,
        &map->map_key,
        &map->descriptor_size,
        &map->descriptor_version);
}

const CHAR16* get_typename_from_memory_type(const EFI_MEMORY_TYPE type) {
    switch(type) {
    case EfiReservedMemoryType:
        return L"EfiReservedMemoryType";
    case EfiLoaderCode:
        return L"EfiLoaderCode";
    case EfiLoaderData:
        return L"EfiLoaderData";
    case EfiBootServicesCode:
        return L"EfiBootServicesCode";
    case EfiBootServicesData:
        return L"EfiBootServicesData";
    case EfiRuntimeServicesCode:
        return L"EfiRuntimeServicesCode";
    case EfiRuntimeServicesData:
        return L"EfiRuntimeServicesData";
    case EfiConventionalMemory:
        return L"EfiConventionalMemory";
    case EfiUnusableMemory:
        return L"EfiUnusableMemory";
    case EfiACPIReclaimMemory:
        return L"EfiACPIReclaimMemory";
    case EfiACPIMemoryNVS:
        return L"EfiACPIMemoryNVS";
    case EfiMemoryMappedIO:
        return L"EfiMemoryMappedIO";
    case EfiMemoryMappedIOPortSpace:
        return L"EfiMemoryMappedIOPortSpace";
    case EfiPalCode:
        return L"EfiPalCode";
    case EfiPersistentMemory:
        return L"EfiPersistentMemory";
    case EfiMaxMemoryType:
        return L"EfiMaxMemoryType";
    default:
        return L"InvalidMemoryType";
    }
}

EFI_STATUS save_memory_map(struct MemoryMap* const map, EFI_FILE_PROTOCOL* const file) {
    {
        CHAR8* header = "Index, Type, Type(name), PhysicalStart, NumberOfPages, Attribute\n";
        UINTN  len    = AsciiStrLen(header);
        file->Write(file, &len, header);
    }
    Print(L"map->buffer = %08lx, map->map_size = %08lx\n", map->buffer, map->map_size);

    {
        int                  i           = 0;
        EFI_PHYSICAL_ADDRESS descriptors = (EFI_PHYSICAL_ADDRESS)map->buffer;
        for(EFI_PHYSICAL_ADDRESS iter = descriptors; iter < descriptors + map->map_size; iter += map->descriptor_size, i += 1) {
            EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)iter;
            CHAR8                  buf[256];
            UINTN                  len = AsciiSPrint(buf, sizeof(buf), "%u, %x, %-ls, %08lx, %lx, %lx\n",
                                                     i, desc->Type, get_typename_from_memory_type(desc->Type),
                                                     desc->PhysicalStart, desc->NumberOfPages,
                                                     desc->Attribute & 0xffffflu);
            file->Write(file, &len, buf);
        }
    }

    return EFI_SUCCESS;
}

EFI_STATUS open_rootdir(const EFI_HANDLE image_handle, EFI_FILE_PROTOCOL** const root) {
    EFI_LOADED_IMAGE_PROTOCOL*       loaded_image;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs;

    gBS->OpenProtocol(
        image_handle,
        &gEfiLoadedImageProtocolGuid,
        (VOID**)&loaded_image,
        image_handle,
        NULL,
        EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

    gBS->OpenProtocol(
        loaded_image->DeviceHandle,
        &gEfiSimpleFileSystemProtocolGuid,
        (VOID**)&fs,
        image_handle,
        NULL,
        EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

    fs->OpenVolume(fs, root);

    return EFI_SUCCESS;
}

EFI_STATUS open_gop(const EFI_HANDLE image_handle, EFI_GRAPHICS_OUTPUT_PROTOCOL** const gop) {
    UINTN       num_gop_handles = 0;
    EFI_HANDLE* gop_handles     = NULL;
    gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &num_gop_handles, &gop_handles);
    gBS->OpenProtocol(gop_handles[0], &gEfiGraphicsOutputProtocolGuid, (VOID**)gop, image_handle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    FreePool(gop_handles);
    return EFI_SUCCESS;
}

const CHAR16* get_pixel_format_string(const EFI_GRAPHICS_PIXEL_FORMAT format) {
    switch(format) {
    case PixelRedGreenBlueReserved8BitPerColor:
        return L"PixelRedGreenBlueReserved8BitPerColor";
    case PixelBlueGreenRedReserved8BitPerColor:
        return L"PixelBlueGreenRedReserved8BitPerColor";
    case PixelBitMask:
        return L"PixelBitMask";
    case PixelBltOnly:
        return L"PixelBltOnly";
    case PixelFormatMax:
        return L"PixelFormatMax";
    default:
        return L"InvalidPixelFormat";
    }
}

void halt(void) {
    while(1) {
        __asm__("hlt");
    }
}

void assert(const EFI_STATUS status, const CHAR16* message) {
    if(!EFI_ERROR(status)) {
        return;
    }
    Print(L"%s: %r\n", message, status);
    while(1) {
        __asm__("hlt");
    }
}

int warn(const EFI_STATUS status, const CHAR16* message) {
    if(!EFI_ERROR(status)) {
        return 0;
    }
    Print(L"%s: %r\n", message, status);
    return 1;
}

#define KERNEL_FILENAME L"\\kernel.elf"

EFI_STATUS EFIAPI uefi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    Print(L"klee loader\n");

    // dump memory map
    CHAR8            memmap_buf[4096 * 4];
    struct MemoryMap memmap = {sizeof(memmap_buf), memmap_buf, 0, 0, 0, 0};
    assert(get_memory_map(&memmap), L"failed to get memory map");

    EFI_FILE_PROTOCOL* root_dir;
    assert(open_rootdir(image_handle, &root_dir), L"failed to open root directory");

    EFI_FILE_PROTOCOL* memmap_file;
    if(!warn(root_dir->Open(root_dir, &memmap_file, L"\\memmap", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0), L"failed to open \\memmap\n")) {
        assert(save_memory_map(&memmap, memmap_file), L"failed to save memory map");
        assert(memmap_file->Close(memmap_file), L"failed to close memory map");
    } else {
        Print(L"error ignored\n");
    }

    // load kernel
    EFI_FILE_PROTOCOL* kernel_file;
    assert(root_dir->Open(root_dir, &kernel_file, KERNEL_FILENAME, EFI_FILE_MODE_READ, 0), L"failed to open kernel");

    // get kernel file size
    UINTN file_info_size = sizeof(EFI_FILE_INFO) + sizeof(KERNEL_FILENAME);
    UINT8 file_info_buffer[file_info_size];
    assert(kernel_file->GetInfo(kernel_file, &gEfiFileInfoGuid, &file_info_size, file_info_buffer), L"failed to get kernel file informations");
    EFI_FILE_INFO* file_info        = (EFI_FILE_INFO*)file_info_buffer;
    UINTN          kernel_file_size = file_info->FileSize;

    // allocate memory and load kernel
    EFI_PHYSICAL_ADDRESS kernel_file_load_addr;
    assert(allocate(&kernel_file_load_addr, kernel_file_size), L"failed to allocate pages for kernel file");
    assert(kernel_file->Read(kernel_file, &kernel_file_size, (VOID*)kernel_file_load_addr), L"failed to read kernel file");
    assert(kernel_file->Close(kernel_file), L"failed to close kernel file");

    // parse elf
    struct ELF*           elf             = (struct ELF*)(kernel_file_load_addr);
    struct ProgramHeader* program_headers = (struct ProgramHeader*)(kernel_file_load_addr + elf->program_header_address);
    for(struct ProgramHeader* program_header = program_headers; program_header < program_headers + elf->program_header_limit; program_header += 1) {
        if(program_header->type != 0x01) {
            // not a loadable segment
            continue;
        }
        Print(L"program_header: offset 0x%0lx, address 0x%0lx, filesize 0x%0lx, memsize 0x%0lx\n", program_header->offset, program_header->p_address, program_header->filesize, program_header->memsize);
        assert(allocate_address(program_header->p_address, program_header->memsize), L"failed to allocate pages for kernel segment");
        CopyMem((VOID*)program_header->p_address, (VOID*)(kernel_file_load_addr + program_header->offset), program_header->filesize);
    }

    // open gop
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
    assert(open_gop(image_handle, &gop), L"failed to open GOP");
    Print(L"resolution: %ux%u, pixel format: %s, %u pixels/line\n", gop->Mode->Info->HorizontalResolution, gop->Mode->Info->VerticalResolution, get_pixel_format_string(gop->Mode->Info->PixelFormat), gop->Mode->Info->PixelsPerScanLine);
    Print(L"frame Buffer: 0x%0lx - 0x%0lx, size: %lu bytes\n", gop->Mode->FrameBufferBase, gop->Mode->FrameBufferBase + gop->Mode->FrameBufferSize, gop->Mode->FrameBufferSize);

    // exit boot service
    if(EFI_ERROR(gBS->ExitBootServices(image_handle, memmap.map_key))) {
        assert(get_memory_map(&memmap), L"failed to get memory map");
        assert(gBS->ExitBootServices(image_handle, memmap.map_key), L" could not exit boot service");
    }

    // call kernel
    typedef __attribute__((sysv_abi)) void EntryPointType(UINT64, UINT64);

    EntryPointType* entry = (EntryPointType*)elf->entry_address;
    entry(gop->Mode->FrameBufferBase, gop->Mode->FrameBufferSize);

    Print(L"done");
    while(1) {
        __asm__("hlt");
    }
    return EFI_SUCCESS;
}
