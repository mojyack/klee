#pragma once
#include "framebuffer.hpp"

inline auto draw_mousecursor(const FramebufferConfig& config, const Point point) -> void {
    constexpr auto mousecursor_width                                            = 15;
    constexpr auto mousecursor_height                                           = 24;
    constexpr char mousecursor_shape[mousecursor_height][mousecursor_width + 1] = {
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
            FRAMEBUFFER_INVOKE(write_pixel, config, point + Point{x, y}, color);
        }
    }
}
