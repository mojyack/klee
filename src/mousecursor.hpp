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

    Framebuffer& fb;
    Point        pos;

    std::array<uint32_t, mousecursor_width * mousecursor_height> backup;
    std::optional<Point>                                         backup_pos;

  public:
    auto move_relative(const Point displacement) -> void {
        const auto [width, height] = fb.get_size();

        pos += displacement;
        if(pos.x < 0) {
            pos.x = 0;
        }
        if(pos.x >= width) {
            pos.x = width - 1;
        }
        if(pos.y < 0) {
            pos.y = 0;
        }
        if(pos.y >= height) {
            pos.y = height - 1;
        }
    }

    auto draw() -> void {
        const auto [width, height] = fb.get_size();

        if(backup_pos) {
            for(auto y = backup_pos->y; y < height && y - backup_pos->y < mousecursor_height; y += 1) {
                for(auto x = backup_pos->x; x < width && x - backup_pos->x < mousecursor_width; x += 1) {
                    fb.write_pixel({x, y}, backup[(y - backup_pos->y) * mousecursor_width + (x - backup_pos->x)]);
                }
            }
        }

        for(auto y = pos.y; y < height && y - pos.y < mousecursor_height; y += 1) {
            for(auto x = pos.x; x < width && x - pos.x < mousecursor_width; x += 1) {
                backup[(y - pos.y) * mousecursor_width + (x - pos.x)] = fb.read_pixel({x, y});
            }
        }
        backup_pos = pos;

        for(auto y = pos.y; y < height && y - pos.y < mousecursor_height; y += 1) {
            for(auto x = pos.x; x < width && x - pos.x < mousecursor_width; x += 1) {
                auto color = uint8_t();
                switch(mousecursor_shape[y - pos.y][x - pos.x]) {
                case '@':
                    color = 0xFF;
                    break;
                case '.':
                    color = 0x00;
                    break;
                default:
                    continue;
                }
                fb.write_pixel({x, y}, color);
            }
        }
    }

    MouseCursor(Framebuffer& fb) : fb(fb), pos(fb.get_size()[0] / 2, fb.get_size()[1] / 2) {}
};
