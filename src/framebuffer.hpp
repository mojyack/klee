#pragma once
#include <array>
#include <concepts>
#include <string_view>

#include "font.hpp"
#include "framebuffer-forward.h"
#include "type.hpp"

template <class T>
concept Color = std::is_same_v<T, RGBColor> || std::is_same_v<T, uint8_t>;

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

    auto get_size() const -> std::array<size_t, 2> {
        return {config.horizontal_resolution, config.vertical_resolution};
    }

    auto write_pixel(const Point point, const RGBColor color) -> void {
        auto p = &config.frame_buffer[find_address(point) * 4];
        p[0]   = color.b;
        p[1]   = color.g;
        p[2]   = color.r;
    }

    auto write_pixel(const Point point, const uint32_t color) -> void {
        auto p = reinterpret_cast<uint32_t*>(&config.frame_buffer[find_address(point) * 4]);
        *p     = color;
    }

    auto write_pixel(const Point point, const uint8_t color) -> void {
        auto p = reinterpret_cast<uint32_t*>(&config.frame_buffer[find_address(point) * 4]);
        *p     = color | color << 8 | color << 16 | color << 24;
    }

    auto read_pixel(const Point point) -> uint32_t {
        return *reinterpret_cast<uint32_t*>(&config.frame_buffer[find_address(point) * 4]);
    }

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
            write_ascii(Point(point.x + font_size[0] * i, point.y), str[i], color);
        }
    }

    Framebuffer(const FramebufferConfig& config) : config(config) {}
};
