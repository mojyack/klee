#include <array>
#include <cstdio>

#include "console.hpp"
#include "print.hpp"

auto console_buf = std::array<uint8_t, sizeof(Console)>();
auto console     = (Console*)(nullptr);

constexpr auto mousecursor_width                                            = 15;
constexpr auto mousecursor_height                                           = 24;
const char     mousecursor_shape[mousecursor_height][mousecursor_width + 1] = {
        "@              ",
        "@@             ",
        "@.@            ",
        "@..@           ",
        "@...@          ",
        "@....@         ",
        "@.....@        ",
        "@......@       ",
        "@.......@      ",
        "@........@     ",
        "@.........@    ",
        "@..........@   ",
        "@...........@  ",
        "@............@ ",
        "@......@@@@@@@@",
        "@......@       ",
        "@....@@.@      ",
        "@...@ @.@      ",
        "@..@   @.@     ",
        "@.@    @.@     ",
        "@@      @.@    ",
        "@       @.@    ",
        "         @.@   ",
        "         @@@   ",
};

extern "C" void kernel_main(const FramebufferConfig& framebuffer_config) {
    console = new(console_buf.data()) Console(framebuffer_config);
    printk("klee\n");

    auto mousecursor = Point{50, 50};
    for(auto y = uint32_t(0); y < mousecursor_height; y += 1) {
        for(auto x = uint32_t(0); x < mousecursor_width; x += 1) {
            auto color = uint8_t();
            switch(mousecursor_shape[y][x]) {
            case '@':
                color = 0xFF;
                break;
            case '.':
                color = 0x00;
                break;
            default:
                continue;
            }
            FRAMEBUFFER_INVOKE(write_pixel, framebuffer_config, mousecursor + Point{x, y}, color);
        }
    }
    while(1) {
        __asm__("hlt");
    }
}
