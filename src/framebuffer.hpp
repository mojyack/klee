#pragma once
#include <array>
#include <concepts>
#include <string_view>

#include "font.hpp"
#include "framebuffer-forward.h"
#include "type.hpp"

template <class T>
concept Color = std::is_same_v<T, RGBColor> || std::is_same_v<T, uint8_t>;

template <PixelFormat>
class Framebuffer {
  private:
    constexpr static std::array<uint32_t, 2> font_size = {8, 16};

    const FramebufferConfig& config;

    auto find_address(const Point point) -> uint64_t {
        return config.pixels_per_scan_line * point.y + point.x;
    }

  public:
    constexpr static auto get_font_size() -> std::array<uint32_t, 2> {
        return font_size;
    }

    auto write_pixel(const Point point, const Color auto color) -> void;

    auto write_rect(const Point a, const Point b, const Color auto color) -> void {
        for(auto y = a.y; y < b.y; y += 1) {
            for(auto x = a.x; x < b.x; x += 1) {
                write_pixel({x, y}, color);
            }
        }
    }

    template <>
    auto write_rect(const Point a, const Point b, const uint8_t color) -> void {
        auto c = uint64_t(color | color << 8 | color << 16 | color << 24) | uint64_t(color | color << 8 | color << 16 | color << 24) << 32;
        for(auto y = a.y; y < b.y; y += 1) {
            for(auto x = a.x; x < b.x; x += 2) {
                auto p = reinterpret_cast<uint64_t*>(&config.frame_buffer[find_address({x, y}) * 4]);
                *p     = c;
            }
        }
    }

    auto write_ascii(const Point point, const char c, const Color auto color) -> void {
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

    auto write_string(const Point point, const std::string_view str, const Color auto color) -> void {
        for(auto i = uint32_t(0); i < str.size(); i += 1) {
            write_ascii({point.x + font_size[0] * i, point.y}, str[i], color);
        }
    }

    Framebuffer(const FramebufferConfig& config) : config(config) {}
};

template <>
template <>
inline auto Framebuffer<PixelRGBResv8BitPerColor>::write_pixel<RGBColor>(const Point point, const RGBColor color) -> void {
    auto p = &config.frame_buffer[find_address(point) * 4];
    p[0]   = color.r;
    p[1]   = color.g;
    p[2]   = color.b;
}

template <>
template <>
inline auto Framebuffer<PixelRGBResv8BitPerColor>::write_pixel<uint8_t>(const Point point, const uint8_t color) -> void {
    auto p = reinterpret_cast<uint32_t*>(&config.frame_buffer[find_address(point) * 4]);
    *p     = color | color << 8 | color << 16 | color << 24;
}

template <>
template <>
inline auto Framebuffer<PixelBGRResv8BitPerColor>::write_pixel<RGBColor>(const Point point, const RGBColor color) -> void {
    auto p = &config.frame_buffer[find_address(point) * 4];
    p[0]   = color.b;
    p[1]   = color.g;
    p[2]   = color.r;
}

template <>
template <>
inline auto Framebuffer<PixelBGRResv8BitPerColor>::write_pixel<uint8_t>(const Point point, const uint8_t color) -> void {
    auto p = reinterpret_cast<uint32_t*>(&config.frame_buffer[find_address(point) * 4]);
    *p     = color | color << 8 | color << 16 | color << 24;
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
