#pragma once
#include <optional>

#include "window.hpp"

class MouseCursor : public Window {
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

  public:
    auto refresh_buffer(const bool focused) -> void override {
    }

    auto is_grabbable(const Point point) const -> bool override {
        return false;
    }

    MouseCursor() : Window(mousecursor_width, mousecursor_height) {
        enable_alpha(true);
        set_position_constraint(PositionConstraints::WithinScreen);

        for(auto y = 0; y < mousecursor_height; y += 1) {
            for(auto x = 0; x < mousecursor_width; x += 1) {
                auto color = uint32_t();
                switch(mousecursor_shape[y][x]) {
                case '@':
                    color = 0xFFFFFFFF;
                    break;
                case '.':
                    color = 0x000000FF;
                    break;
                default:
                    continue;
                }
                draw_pixel({x, y}, RGBAColor(color));
            }
        }
    }
};
