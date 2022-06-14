#pragma once
#include <string_view>

#include "font.hpp"
#include "framebuffer-forward.h"
#include "type.hpp"

template <PixelFormat>
class Framebuffer {
  private:
    constexpr static int font_size[2] = {8, 16};

    const FramebufferConfig& config;

    auto find_address(const Point point) -> uint64_t {
        return config.pixels_per_scan_line * point.y + point.x;
    }

  public:
    auto write_pixel(const Point point, const Color color) -> void;
    auto write_rect(const Point a, const Point b, const Color color) -> void {
        for(auto y = a.y; y < b.y; y += 1) {
            for(auto x = a.x; x < b.x; x += 1) {
                write_pixel({x, y}, color);
            }
        }
    }
    auto write_ascii(const Point point, const char c, const Color color) -> void {
        const auto font = get_font(c);
        for(auto y = 0; y < font_size[1]; y += 1) {
            for(auto x = 0; x < font_size[0]; x += 1) {
                if(!((font[y] << x) & 0x80u)) {
                    continue;
                }
                write_pixel({x + point.x, y + point.y}, color);
            }
        }
    }
    auto write_string(const Point point, std::string_view str, const Color color) -> void {
        for(auto i = uint32_t(0); i < str.size(); i += 1) {
            write_ascii({point.x + font_size[0] * i, point.y}, str[i], color);
        }
    }
    Framebuffer(const FramebufferConfig& config) : config(config) {}
};

template <>
inline auto Framebuffer<PixelRGBResv8BitPerColor>::write_pixel(const Point point, const Color color) -> void {
    auto p = &config.frame_buffer[find_address(point) * 4];
    p[0]   = color.r;
    p[1]   = color.g;
    p[2]   = color.b;
}

template <>
inline auto Framebuffer<PixelBGRResv8BitPerColor>::write_pixel(const Point point, const Color color) -> void {
    auto p = &config.frame_buffer[find_address(point) * 4];
    p[0]   = color.b;
    p[1]   = color.g;
    p[2]   = color.r;
}

#define FRAMEBUFFER_INVOKE(fn, config, ...)                            \
    switch(config.pixel_format) {                                      \
    case PixelRGBResv8BitPerColor: {                                   \
        Framebuffer<PixelRGBResv8BitPerColor>(config).fn(__VA_ARGS__); \
    }                                                                  \
    case PixelBGRResv8BitPerColor: {                                   \
        Framebuffer<PixelBGRResv8BitPerColor>(config).fn(__VA_ARGS__); \
    }                                                                  \
    }
