#pragma once
#include <cstdint>

#include "error.hpp"

namespace klee {
struct SyscallResult {
    uint64_t    value;
    Error::Code error;
};

enum class OpenMode : uint64_t {
    Read,
    Write,
};
} // namespace klee

extern "C" {
auto syscall_printk(const char* str) -> klee::SyscallResult;
auto syscall_exit() -> klee::SyscallResult;
auto syscall_open(const char* path, uint64_t path_len, klee::OpenMode mode) -> klee::SyscallResult;
auto syscall_read(uint64_t );
}
