#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "memory.h"

EFI_STATUS allocate(EFI_PHYSICAL_ADDRESS* address, const UINT64 size) {
    return gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, (size + 0x1000 - 1) / 0x1000, address);
}

EFI_STATUS allocate_address(const EFI_PHYSICAL_ADDRESS address, const UINT64 size) {
    EFI_PHYSICAL_ADDRESS page_address      = (address / 0x1000) * 0x1000;
    UINT64               alloc_size        = address - page_address + size;
    EFI_PHYSICAL_ADDRESS allocated_address = page_address;
    return gBS->AllocatePages(AllocateAddress, EfiLoaderData, (alloc_size + 0x1000 - 1) / 0x1000, &allocated_address);
}

EFI_STATUS allocate_pool(VOID** address, const UINT64 size) {
    return gBS->AllocatePool(EfiLoaderData, size, address);
}

EFI_STATUS free_pool(VOID* const address) {
    return gBS->FreePool(address);
}
