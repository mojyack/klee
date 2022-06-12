#include <cstdint>

//auto boot(const uint64_t framebuffer_base, const uint64_t framebuffer_size) -> void {
//    const auto framebuffer = reinterpret_cast<uint8_t*>(framebuffer_base);
//    for(auto i = uint64_t(0); i < framebuffer_size; i += 1) {
//        framebuffer[i] = 0xFF;
//    }
//    while(1) {
//        __asm__("hlt");
//    }
//}

extern "C" void kernel_main(const uint64_t framebuffer_base, const uint64_t framebuffer_size) {
    const auto framebuffer = reinterpret_cast<uint8_t*>(framebuffer_base);
    for(auto i = uint64_t(0); i < framebuffer_size; i += 1) {
        framebuffer[i] = 0xFF;
    }
//    boot(framebuffer_base, framebuffer_size);
}
