#pragma once
#include "asmcode.h"
#include "msr.hpp"
#include "print.hpp"
#include "segment.hpp"

namespace syscall {
struct Result {
    uint64_t value;
    int      error;
};

using SyscallFunc = Result(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

inline auto syscall_printk(const uint64_t arg0, const uint64_t arg1, const uint64_t arg2, const uint64_t arg3, const uint64_t arg4, const uint64_t arg5) -> Result {
    const auto text = reinterpret_cast<const char*>(arg0);
    printk(text);
    return {0, 0};
}

inline auto syscall_exit(const uint64_t arg0, const uint64_t arg1, const uint64_t arg2, const uint64_t arg3, const uint64_t arg4, const uint64_t arg5) -> Result {
    auto& task = task::manager->get_current_task();
    task.exit();
    return {0, 0};
}

inline auto initialize_syscall() -> void {
    write_msr(MSR::EFER, ExtendedFeatureEnableRegister{.bits = {.sce = 1, .lme = 1, .lma = 1}}.data);
    write_msr(MSR::LSTAR, reinterpret_cast<uint64_t>(syscall_entry));
    write_msr(MSR::FMASK, 0);

    const auto star_syscall = segment::SegmentSelector{.bits = {0, 0, segment::SegmentNumber::KernelCode}};
    const auto star_sysret  = segment::SegmentSelector{.bits = {3, 0, segment::SegmentNumber::UserStack - 1}};
    write_msr(MSR::STAR, SystemTargetAddressRegister{.bits = {.syscall_csss = star_syscall.data, .sysret_csss = star_sysret.data}}.data);
}
} // namespace syscall
