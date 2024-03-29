#pragma once
#include <cstddef>
#include <cstdint>

namespace ahci::internal {
enum class FISType : uint8_t {
    RegH2D   = 0x27, // Register FIS - host to device
    RegD2H   = 0x34, // Register FIS - device to host
    DMAAct   = 0x39, // DMA activate FIS - device to host
    DMASetup = 0x41, // DMA setup FIS - bidirectional
    Data     = 0x46, // Data FIS - bidirectional
    BIST     = 0x58, // BIST activate FIS - bidirectional
    PIOSetup = 0x5F, // PIO setup FIS - device to host
    DevBits  = 0xA1, // Set device bits FIS - device to host
};

struct RegH2DFIS {
    // DWORD 0
    FISType fis_type = FISType::RegH2D;

    uint8_t pmport : 4; // Port multiplier
    uint8_t rsv0 : 3;   // Reserved
    uint8_t c : 1;      // 1: Command, 0: Control

    uint8_t command;  // Command register
    uint8_t featurel; // Feature register, 7:0

    // DWORD 1
    uint8_t lba0;   // LBA low register, 7:0
    uint8_t lba1;   // LBA mid register, 15:8
    uint8_t lba2;   // LBA high register, 23:16
    uint8_t device; // Device register

    // DWORD 2
    uint8_t lba3;     // LBA register, 31:24
    uint8_t lba4;     // LBA register, 39:32
    uint8_t lba5;     // LBA register, 47:40
    uint8_t featureh; // Feature register, 15:8

    // DWORD 3
    uint8_t countl;  // Count register, 7:0
    uint8_t counth;  // Count register, 15:8
    uint8_t icc;     // Isochronous command completion
    uint8_t control; // Control register

    // DWORD 4
    uint8_t rsv1[4]; // Reserved
} __attribute__((packed));

struct RegD2HFIS {
    // DWORD 0
    FISType fis_type = FISType::RegD2H;

    uint8_t pmport : 4; // Port multiplier
    uint8_t rsv0 : 2;   // Reserved
    uint8_t i : 1;      // Interrupt bit
    uint8_t rsv1 : 1;   // Reserved

    uint8_t status; // Status register
    uint8_t error;  // Error register

    // DWORD 1
    uint8_t lba0;   // LBA low register, 7:0
    uint8_t lba1;   // LBA mid register, 15:8
    uint8_t lba2;   // LBA high register, 23:16
    uint8_t device; // Device register

    // DWORD 2
    uint8_t lba3; // LBA register, 31:24
    uint8_t lba4; // LBA register, 39:32
    uint8_t lba5; // LBA register, 47:40
    uint8_t rsv2; // Reserved

    // DWORD 3
    uint8_t countl;  // Count register, 7:0
    uint8_t counth;  // Count register, 15:8
    uint8_t rsv3[2]; // Reserved

    // DWORD 4
    uint8_t rsv4[4]; // Reserved
} __attribute__((packed));

struct DMAActFIS {
    // TODO
} __attribute__((packed));

struct DMASetupFIS {
    // DWORD 0
    FISType fis_type = FISType::DMASetup;

    uint8_t pmport : 4; // Port multiplier
    uint8_t rsv0 : 1;   // Reserved
    uint8_t d : 1;      // Data transfer direction, 1 - device to host
    uint8_t i : 1;      // Interrupt bit
    uint8_t a : 1;      // Auto-activate. Specifies if DMA Activate FIS is needed

    uint8_t rsved[2]; // Reserved

    // DWORD 1&2

    uint64_t DMAbufferID; // DMA Buffer Identifier. Used to Identify DMA buffer in host memory.
                          // SATA Spec says host specific and not in Spec. Trying AHCI spec might work.

    // DWORD 3
    uint32_t rsvd; // More reserved

    // DWORD 4
    uint32_t DMAbufOffset; // Byte offset into buffer. First 2 bits must be 0

    // DWORD 5
    uint32_t TransferCount; // Number of bytes to transfer. Bit 0 must be 0

    // DWORD 6
    uint32_t resvd; // Reserved
} __attribute__((packed));

struct DataFIS {
    // DWORD 0
    FISType fis_type = FISType::Data;

    uint8_t pmport : 4; // Port multiplier
    uint8_t rsv0 : 4;   // Reserved

    uint8_t rsv1[2]; // Reserved

    // DWORD 1 ~ N
    uint32_t data[1]; // Payload
} __attribute__((packed));

struct BISTFIS {
    // TODO
} __attribute__((packed));

struct PIOSetupFIS {
    // DWORD 0
    FISType fis_type = FISType::PIOSetup;

    uint8_t pmport : 4; // Port multiplier
    uint8_t rsv0 : 1;   // Reserved
    uint8_t d : 1;      // Data transfer direction, 1 - device to host
    uint8_t i : 1;      // Interrupt bit
    uint8_t rsv1 : 1;

    uint8_t status; // Status register
    uint8_t error;  // Error register

    // DWORD 1
    uint8_t lba0;   // LBA low register, 7:0
    uint8_t lba1;   // LBA mid register, 15:8
    uint8_t lba2;   // LBA high register, 23:16
    uint8_t device; // Device register

    // DWORD 2
    uint8_t lba3; // LBA register, 31:24
    uint8_t lba4; // LBA register, 39:32
    uint8_t lba5; // LBA register, 47:40
    uint8_t rsv2; // Reserved

    // DWORD 3
    uint8_t countl;   // Count register, 7:0
    uint8_t counth;   // Count register, 15:8
    uint8_t rsv3;     // Reserved
    uint8_t e_status; // New value of status register

    // DWORD 4
    uint16_t tc;      // Transfer count
    uint8_t  rsv4[2]; // Reserved
} __attribute__((packed));

struct DevBitsFIS {
    // TODO
    uint32_t bits[2];
} __attribute__((packed));

struct alignas(0x100) HBAFIS {
    // 0x00
    DMASetupFIS dsfis; // DMA Setup FIS
    uint8_t     pad0[4];

    // 0x20
    PIOSetupFIS psfis; // PIO Setup FIS
    uint8_t     pad1[12];

    // 0x40
    RegD2HFIS rfis; // Register – Device to Host FIS
    uint8_t   pad2[4];

    // 0x58
    DevBitsFIS sdbfis; // Set Device Bit FIS

    // 0x60
    uint8_t ufis[64]; // Unknown FIS

    // 0xA0
    uint8_t rsv[0x100 - 0xA0];
} __attribute__((packed));
} // namespace ahci::internal
