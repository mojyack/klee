#include <array>

// #include <cstdio>

extern "C" {
auto syscall_printk(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) ->int64_t;
auto syscall_exit(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) ->int64_t;

auto start(const uint64_t task, const int64_t data) -> void {
    syscall_printk(reinterpret_cast<uint64_t>("Hello via syscall!\n"), 0, 0, 0, 0, 0);
    auto fake = 0xFFFF800000000000u;
    auto buf = std::array<char, 7>{"iter 0"};
    for(auto i = 0; i < 8; i += 1) {
        buf[5] = '0' + i;
        syscall_printk(fake + 0x1000 * i, 0, 0, 0, 0, 0);
        syscall_printk(reinterpret_cast<uint64_t>(buf.data()), 0, 0, 0, 0, 0);
    }
    auto addr = 0;
    *reinterpret_cast<int*>(addr) = 0xFF;
    syscall_exit(1, 0, 0, 0, 0, 0);
    return;
}
}
