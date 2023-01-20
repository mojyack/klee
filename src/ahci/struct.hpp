#pragma once
#include <cstddef>
#include <cstdint>

namespace ahci::internal {
struct HBAPort {
    enum class Signature : uint32_t {
        ATA     = 0x00000101,
        ATAPI   = 0xEB140101,
        ATASEMB = 0xC33C0101,
        PM      = 0x96690101,
    };

    union Cmd {
        uint32_t data;
        struct {
            uint32_t st : 1;        // Start
            uint32_t sud : 1;       // Spin-Up Device
            uint32_t pod : 1;       // Power On Device
            uint32_t clo : 1;       // Command List Override
            uint32_t fre : 1;       // FIS Receive Enable
            uint32_t reserved1 : 3; // reserved
            uint32_t css : 5;       // Current Command Slot
            uint32_t mpss : 1;      // Mechanical Presence Switch State
            uint32_t fr : 1;        // FIS Receive Running
            uint32_t cr : 1;        // Command List Running
            uint32_t cps : 1;       // Cold Presence State
            uint32_t pma : 1;       // Port Multiplier Attached
            uint32_t hpcp : 1;      // Hot Plug Capable Port
            uint32_t mpsp : 1;      // Mechanical Presence Switch Attached to Port
            uint32_t cpd : 1;       // Cold Presence Detection
            uint32_t esp : 1;       // External SATA Port
            uint32_t reserved2 : 2; // reserved
            uint32_t atapi : 1;     // Device is ATAPI
            uint32_t dlae : 1;      // Drive LED on ATAPI Enable
            uint32_t alpe : 1;      // Aggressive Link Power Management Enable
            uint32_t asp : 1;       // Aggressive Slumber / Partial
            uint32_t icc : 4;       // Interface Communication Control
        } __attribute__((packed)) bits;
    };

    union InterruptStatus {
        uint32_t data;
        struct {
            uint32_t dhrs : 1;       // Device to Host Register FIS Interrupt
            uint32_t pss : 1;        // PIO Setup FIS Interrupt
            uint32_t dss : 1;        // DMA Setup FIS Interrupt
            uint32_t sdbs : 1;       // Set Device Bits Interrupt
            uint32_t ufs : 1;        // Unknown FIS Interrupt
            uint32_t dps : 1;        // Descriptor Processed
            uint32_t pcs : 1;        // Port Connect Change Status
            uint32_t dmps : 1;       // Device Mechanical Presence Status
            uint32_t reserved1 : 14; //
            uint32_t prcs : 1;       // PhyRdy Change Status
            uint32_t ipms : 1;       // Incorrect Port Multiplier Status
            uint32_t ofs : 1;        // Overflow Status
            uint32_t reserved2 : 1;  //
            uint32_t infs : 1;       // Interface Non-fatal Error Status
            uint32_t ifs : 1;        // Interface Fatal Error Status
            uint32_t hbds : 1;       // Host Bus Data Error Status
            uint32_t hbfs : 1;       // Host Bus Fatal Error Status
            uint32_t tfes : 1;       // Task File Error Status
            uint32_t cpds : 1;       // Cold Port Detect Status
        } __attribute__((packed)) bits;
    };

    uint32_t        clb;       // 0x00, command list base address, 1K-byte aligned
    uint32_t        clbu;      // 0x04, command list base address upper 32 bits
    uint32_t        fb;        // 0x08, FIS base address, 256-byte aligned
    uint32_t        fbu;       // 0x0C, FIS base address upper 32 bits
    InterruptStatus is;        // 0x10, interrupt status
    uint32_t        ie;        // 0x14, interrupt enable
    Cmd             cmd;       // 0x18, command and status
    uint32_t        rsv0;      // 0x1C, Reserved
    uint32_t        tfd;       // 0x20, task file data
    Signature       sig;       // 0x24, signature
    uint32_t        ssts;      // 0x28, SATA status (SCR0:SStatus)
    uint32_t        sctl;      // 0x2C, SATA control (SCR2:SControl)
    uint32_t        serr;      // 0x30, SATA error (SCR1:SError)
    uint32_t        sact;      // 0x34, SATA active (SCR3:SActive)
    uint32_t        ci;        // 0x38, command issue
    uint32_t        sntf;      // 0x3C, SATA notification (SCR4:SNotification)
    uint32_t        fbs;       // 0x40, FIS-based switch control
    uint32_t        rsv1[11];  // 0x44 ~ 0x6F, Reserved
    uint32_t        vendor[4]; // 0x70 ~ 0x7F, vendor specific

    auto start() volatile -> void {
        while(cmd.bits.cr) {
            //
        }
        cmd.bits.fre = 1;
        cmd.bits.st  = 1;
    }

    auto stop() volatile -> void {
        cmd.bits.st  = 0;
        cmd.bits.fre = 0;
        while(cmd.bits.fr || cmd.bits.cr) {
            //
        }
    }

    auto find_lazy_command_slot(const uint32_t num_slots) volatile -> int {
        auto slots = sact | ci;
        for(auto i = 0; i < num_slots; i += 1) {
            if((slots & 1) == 0) {
                return i;
            }
            slots >>= 1;
        }
        return -1;
    }
} __attribute__((packed));

struct HBAHeader {
    struct Capability {
        uint32_t np : 5;    // Number of Ports
        uint32_t sxs : 1;   // Supports External SATA
        uint32_t ems : 1;   // Enclosure Management Supported
        uint32_t cccs : 1;  // Command Completion Coalescing Supported
        uint32_t ncs : 5;   // Number of Command Slots
        uint32_t psc : 1;   // Partial State Capable
        uint32_t ssc : 1;   // Slumber State Capable
        uint32_t pmd : 1;   // PIO Multiple DRQ Block
        uint32_t fbss : 1;  // FIS-based Switching Supported
        uint32_t spm : 1;   // Supports Port Multiplier
        uint32_t sam : 1;   // Supports AHCI mode only
        uint32_t snzo : 1;  // Supports Non-Zero DMA Offsets
        uint32_t iss : 4;   // Interface Speed Support
        uint32_t sclo : 1;  // Supports Command List Override
        uint32_t sal : 1;   // Supports Activity LED
        uint32_t salp : 1;  // Supports Aggressive Link Power Management
        uint32_t sss : 1;   // Supports Staggered Spin-up
        uint32_t smps : 1;  // Supports Mechanical Presence Switch
        uint32_t ssntf : 1; // Supports SNotification Register
        uint32_t sncq : 1;  // Supports Native Command Queuing
        uint32_t s64a : 1;  // Supports 64-bit Addressing
    } __attribute__((packed));

    union GlobalHostControl {
        uint32_t data;
        struct {
            uint32_t hr : 1;        // HBA Reset
            uint32_t ie : 1;        // Interrupt Enable
            uint32_t mrsm : 1;      // MSI Revert to Single Message
            uint32_t reserved : 28; //
            uint32_t ae : 1;        // AHCI Enable
        } __attribute__((packed));
    };

    // 0x00 - 0x2B, Generic Host Control
    Capability        cap;     // 0x00, Host capability
    GlobalHostControl ghc;     // 0x04, Global host control
    uint32_t          is;      // 0x08, Interrupt status
    uint32_t          pi;      // 0x0C, Port implemented
    uint32_t          vs;      // 0x10, Version
    uint32_t          ccc_ctl; // 0x14, Command completion coalescing control
    uint32_t          ccc_pts; // 0x18, Command completion coalescing ports
    uint32_t          em_loc;  // 0x1C, Enclosure management location
    uint32_t          em_ctl;  // 0x20, Enclosure management control
    uint32_t          cap2;    // 0x24, Host capabilities extended
    uint32_t          bohc;    // 0x28, BIOS/OS handoff control and status

    // 0x2C - 0x9F, Reserved
    uint8_t rsv[0xA0 - 0x2C];

    // 0xA0 - 0xFF, Vendor specific registers
    uint8_t vendor[0x100 - 0xA0];

    // 0x100 - 0x10FF, Port control registers
    HBAPort ports[]; // 1 ~ 32
} __attribute__((packed));

struct CommandHeader {
    // DW0
    uint8_t cfl : 5; // Command FIS length in DWORDS, 2 ~ 16
    uint8_t a : 1;   // ATAPI
    uint8_t w : 1;   // Write, 1: H2D, 0: D2H
    uint8_t p : 1;   // Prefetchable

    uint8_t r : 1;    // Reset
    uint8_t b : 1;    // BIST
    uint8_t c : 1;    // Clear busy upon R_OK
    uint8_t rsv0 : 1; // Reserved
    uint8_t pmp : 4;  // Port multiplier port

    uint16_t prdtl; // Physical region descriptor table length in entries

    // DW1
    volatile uint32_t prdbc; // Physical region descriptor byte count transferred

    // DW2, 3
    uint32_t ctba;  // Command table descriptor base address
    uint32_t ctbau; // Command table descriptor base address upper 32 bits

    // DW4 - 7
    uint32_t rsv1[4]; // Reserved
} __attribute__((packed));

struct PRDTEntry {
    uint32_t dba;  // Data base address
    uint32_t dbau; // Data base address upper 32 bits
    uint32_t rsv0; // Reserved

    // DW3
    uint32_t dbc : 22; // Byte count, 4M max
    uint32_t rsv1 : 9; // Reserved
    uint32_t i : 1;    // Interrupt on completion
} __attribute__((packed));

struct alignas(0x80) CommandTable {
    // 0x00
    uint8_t cfis[64]; // Command FIS

    // 0x40
    uint8_t acmd[16]; // ATAPI command, 12 or 16 bytes

    // 0x50
    uint8_t rsv[48]; // Reserved

    // 0x80
    PRDTEntry prdt_entry[]; // Physical region descriptor table entries, 0 ~ 65535
} __attribute__((packed));
} // namespace ahci::internal
