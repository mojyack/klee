#pragma once
#include <Uefi.h>

EFI_STATUS allocate(EFI_PHYSICAL_ADDRESS* address, UINT64 size);
EFI_STATUS allocate_address(EFI_PHYSICAL_ADDRESS address, UINT64 size);
EFI_STATUS allocate_pool(VOID** address, UINT64 size);
EFI_STATUS free_pool(VOID* address);
