#pragma once
#include <array>
#include <concepts>
#include <string_view>
#include <vector>

#include "framebuffer-forward.h"
#include "type.hpp"

class Framebuffer {
  private:
    FramebufferConfig config;

    std::vector<uint8_t> backbuffer;

    auto find_address(const Point point) -> uint64_t {
        return config.pixels_per_scan_line * point.y + point.x;
    }

  public:
    auto get_size() const -> std::array<size_t, 2> {
        return {config.horizontal_resolution, config.vertical_resolution};
    }

    auto write_pixel(const Point point, const RGBColor color) -> void {
        auto p = &backbuffer[find_address(point) * 4];
        p[0]   = color.b;
        p[1]   = color.g;
        p[2]   = color.r;
    }

    auto write_pixel(const Point point, const uint32_t color) -> void {
        auto p = reinterpret_cast<uint32_t*>(&backbuffer[find_address(point) * 4]);
        *p     = color;
    }

    auto write_pixel(const Point point, const uint8_t color) -> void {
        auto p = reinterpret_cast<uint32_t*>(&backbuffer[find_address(point) * 4]);
        *p     = color | color << 8 | color << 16 | color << 24;
    }

    auto read_pixel(const Point point) -> uint32_t {
        return *reinterpret_cast<uint32_t*>(&backbuffer[find_address(point) * 4]);
    }

    auto write_rect(const Point a, const Point b, const RGBColor color) -> void {
        for(auto y = a.y; y < b.y; y += 1) {
            for(auto x = a.x; x < b.x; x += 1) {
                write_pixel({x, y}, color);
            }
        }
    }

    auto write_rect(const Point a, const Point b, const uint8_t color) -> void {
        auto c = uint64_t(color | color << 8 | color << 16 | color << 24) | uint64_t(color | color << 8 | color << 16 | color << 24) << 32;
        for(auto y = a.y; y < b.y; y += 1) {
            for(auto x = a.x; x < b.x; x += 2) {
                auto p = reinterpret_cast<uint64_t*>(&backbuffer[find_address({x, y}) * 4]);
                *p     = c;
            }
        }
    }

    auto copy_array(const uint32_t* const source, const Point dest, const size_t len) -> void {
        memcpy(backbuffer.data() + find_address(dest) * 4, source, len * 4);
    }

    auto swap() -> void {
        memcpy(config.frame_buffer, backbuffer.data(), backbuffer.size());
    }

    Framebuffer(const FramebufferConfig& config) : config(config),
                                                   backbuffer(config.horizontal_resolution * config.vertical_resolution * 4) {}
};

inline auto framebuffer = (Framebuffer*)(nullptr);
