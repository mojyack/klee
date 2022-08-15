#include <array>
//
// auto strcmp(const char* const a, const char* const b) -> int {
//    auto i = 0;
//    for(; a[i] != 0 && b[i] != 0; i += 1) {
//        if(a[i] != b[i]) {
//            return a[i] - b[i];
//        }
//    }
//    return a[i] - b[i];
//}
//
// auto atol(const char* const s) -> long {
//    auto v = long(0);
//    for(auto i = 0; s[i] != 0; ++i) {
//        v = v * 10 + (s[i] - '0');
//    }
//    return v;
//}
//
// auto stack_ptr = int();
// auto stack     = std::array<long, 100>();

extern "C" {
auto syscall_printk(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) ->int64_t;

volatile int a = 0;
auto start(const uint64_t task, const int64_t data) -> void {
    syscall_printk(reinterpret_cast<uint64_t>("Hello via syscall!\n"), 0, 0, 0, 0, 0);
    while(true) {
        a = 0;
    }
    return;
}
}
