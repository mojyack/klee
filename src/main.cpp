#include <array>
#include <cstdio>

#include "console.hpp"
#include "print.hpp"

auto console_buf = std::array<uint8_t, sizeof(Console)>();
auto console     = (Console*)(nullptr);

extern "C" void kernel_main(const FramebufferConfig& framebuffer_config) {
    console = new(console_buf.data()) Console(framebuffer_config);
    for(auto i = 0; i < 100000; i += 1) {
        printk("%d hello \n", i);
    }
    while(1) {
        __asm__("hlt");
    }
}
