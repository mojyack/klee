#pragma once
#include <cstdint>

enum MSR : uint32_t {
    EFER  = 0xC0000080, // Extended Feature Enable Register
    STAR  = 0xC0000081, // System Target Address Register legacy mode SYSCALL target
    LSTAR = 0xC0000082, // long mode SYSCALL target
    FMASK = 0xC0000084, // EFLAGS mask for syscall
};

union ExtendedFeatureEnableRegister {
    uint64_t data;
    struct {
        uint64_t sce : 1;    // system call extensions
        uint64_t dpe : 1;    // data prefetch enable (amd k6 only)
        uint64_t sewbed : 1; // speculative ewbe# disable (amd k6 only)
        uint64_t gewbed : 1; // global ewbe# disable (amd k6 only)
        uint64_t l2d : 1;    // l2 cache disable
        uint64_t reserved1 : 3;
        uint64_t lme : 1; // long mode enable
        uint64_t reserved2 : 1;
        uint64_t lma : 1;   // long mode active
        uint64_t nxe : 1;   // no-execute enable
        uint64_t svme : 1;  // secure virtual machine enable
        uint64_t lmsle : 1; // long mode segment limit enable
        uint64_t ffxsr : 1; // fast FXSAVE/FXRSTOR
        uint64_t tce : 1;   // translation cache extension
        uint64_t reserved3 : 48;
    } __attribute__((packed)) bits;
};

union SystemTargetAddressRegister {
    uint64_t data;
    struct {
        uint64_t eip : 32;
        uint64_t syscall_csss : 16;
        uint64_t sysret_csss : 16;
    } __attribute__((packed)) bits;
};
