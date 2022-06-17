#pragma once
#include <optional>

#include "framebuffer.hpp"

class MouseCursor {
  private:
    constexpr static auto mousecursor_width                                            = 15;
    constexpr static auto mousecursor_height                                           = 24;
    constexpr static char mousecursor_shape[mousecursor_height][mousecursor_width + 1] = {
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

    const FramebufferConfig& config;
    Point                    pos;

    std::array<uint32_t, mousecursor_width * mousecursor_height> backup;
    std::optional<Point>                                         backup_pos;

  public:
    auto move_relative(const Point displacement) -> void {
        pos += displacement;
        if(pos.x < 0) {
            pos.x = 0;
        }
        if(pos.y < 0) {
            pos.y = 0;
        }
    }

    auto draw() -> void {
        if(backup_pos) {
            for(auto y = backup_pos->y; y < config.vertical_resolution && y - backup_pos->y < mousecursor_height; y += 1) {
                for(auto x = backup_pos->x; x < config.horizontal_resolution && x - backup_pos->x < mousecursor_width; x += 1) {
                    FRAMEBUFFER_INVOKE(write_pixel, config, {x, y}, backup[(y - backup_pos->y) * mousecursor_width + (x - backup_pos->x)]);
                }
            }
        }

        for(auto y = pos.y; y < config.vertical_resolution && y - pos.y < mousecursor_height; y += 1) {
            for(auto x = pos.x; x < config.horizontal_resolution && x - pos.x < mousecursor_width; x += 1) {
                FRAMEBUFFER_INVOKE(read_pixel, config, {x, y}, backup[(y - pos.y) * mousecursor_width + (x - pos.x)]);
            }
        }
        backup_pos = pos;

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
                FRAMEBUFFER_INVOKE(write_pixel, config, pos + Point(x, y), color);
            }
        }
    }

    MouseCursor(const FramebufferConfig& config) : config(config), pos(config.horizontal_resolution / 2, config.vertical_resolution / 2) {}
};
