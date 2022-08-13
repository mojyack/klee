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

using Print = void(const char*, ...);

extern "C" {
auto start(const uint64_t task, const int64_t data) -> int {
    auto print = reinterpret_cast<Print*>(data);

    print("Hello, this is rpn!\n");
    print("start() at 0x%lX\n", &start);
    print("Bye!\n");
    return 0;
}
}
