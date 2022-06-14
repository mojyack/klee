#include <array>
#include <cstdio>

#include "framebuffer.hpp"

extern "C" void kernel_main(const FramebufferConfig& framebuffer_config) {
    FRAMEBUFFER_INVOKE(write_rect, framebuffer_config, {0, 0}, {framebuffer_config.horizontal_resolution, framebuffer_config.vertical_resolution}, 0x000000);
    for(auto c = '!'; c <= '~'; c += 1) {
        const auto i = static_cast<uint32_t>(c - '!');
        FRAMEBUFFER_INVOKE(write_ascii, framebuffer_config, {8 * i, 20}, c, 0xFFFFFF);
    }
    FRAMEBUFFER_INVOKE(write_string, framebuffer_config, {0, 40}, "Hello World!", 0xFFFFFF);

    auto buf = std::array<char, 128>();
    sprintf(buf.data(), "[%04d-%02d-%02d] %s", 2022, 06, 14, "klee kernel");
    FRAMEBUFFER_INVOKE(write_string, framebuffer_config, {0, 60}, buf.data(), 0xFFFFFF);

    while(1) {
        __asm__("hlt");
    }
}
