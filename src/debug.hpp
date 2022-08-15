#pragma once
#include <string_view>

#include "font.hpp"
#include "framebuffer.hpp"
#include "uefi/framebuffer.h"

namespace debug {
class Framebuffer : public ::Framebuffer {
  private:
    FramebufferConfig config;

    auto find_pointer(const Point point, const bool flip) -> uint8_t* override {
        return config.frame_buffer + (point.y * config.pixels_per_scan_line + point.x) * 4;
    }

    auto do_swap(const bool flip) -> bool override {
        return true;
    }

  public:
    auto get_size() const -> std::array<size_t, 2> override {
        return {config.horizontal_resolution, config.vertical_resolution};
    }

    Framebuffer(const FramebufferConfig& config) : config(config) {}
};

inline auto fb = (Framebuffer*)(nullptr);

template <std::integral T>
auto print_hex(const T value) -> std::array<char, sizeof(T) * 2 + 1> {
    auto r = std::array<char, sizeof(T) * 2 + 1>();

    for(auto i = 0; i < sizeof(T) * 2; i += 1) {
        auto c = (value >> 4 * (sizeof(T) * 2 - i - 1)) & 0x0Fu;
        if(c >= 10) {
            c += 'a' - 10;
        } else {
            c += '0';
        }
        r[i] = c;
    }

    r.back() = '\0';

    return r;
}

inline auto draw_ascii(const Point point, const char c) -> void {
    const auto font = get_font(c);
    for(auto y = 0; y < get_font_size()[1]; y += 1) {
        for(auto x = 0; x < get_font_size()[0]; x += 1) {
            if(!((font[y] << x) & 0x80u)) {
                continue;
            }
            fb->write_pixel({x + point.x, y + point.y}, RGBColor(0xFFFFFF));
        }
    }
}

inline auto draw_string(const Point point, const std::string_view str) -> void {
    for(auto i = uint32_t(0); i < str.size(); i += 1) {
        draw_ascii(Point(point.x + get_font_size()[0] * i, point.y), str[i]);
    }
}

inline auto debug_print(const std::string_view str) -> void {
    if(fb == nullptr) {
        return;
    }

    static auto pos = 0;

    auto increment_pos = [](const int a, const auto b) -> void {
        pos += a;
        if(pos + b >= fb->get_size()[1]) {
            pos = 0;
        }
    };

    const auto size      = fb->get_size();
    const auto font_size = get_font_size();
    fb->write_rect(Point(0, pos), Point(size[0], pos + font_size[1]), RGBColor(0x000000));
    draw_string(Point(0, pos), str);
    increment_pos(font_size[1], 2);
    fb->write_rect(Point(0, pos), Point(size[0], pos + 2), RGBColor(0xFFFFFF));
}
} // namespace debug
