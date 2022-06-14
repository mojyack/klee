#include <cstdint>

#include "framebuffer.h"

struct Point {
    uint32_t x;
    uint32_t y;
};

struct Color {
    char r;
    char g;
    char b;

    Color(const char r, const char g, const char b) : r(r), g(g), b(b) {}
    Color(const int32_t color) : r((color >> 16) & 0xFF), g((color >> 8) & 0xFF), b((color >> 0) & 0xFF) {}
};

template <PixelFormat>
class Framebuffer {
  private:
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
    Framebuffer(const FramebufferConfig& config) : config(config) {}
};

template <>
auto Framebuffer<PixelRGBResv8BitPerColor>::write_pixel(const Point point, const Color color) -> void {
    auto p = &config.frame_buffer[find_address(point) * 4];
    p[0]   = color.r;
    p[1]   = color.g;
    p[2]   = color.b;
}

template <>
auto Framebuffer<PixelBGRResv8BitPerColor>::write_pixel(const Point point, const Color color) -> void {
    auto p = &config.frame_buffer[find_address(point) * 4];
    p[0]   = color.b;
    p[1]   = color.g;
    p[2]   = color.r;
}

auto write_rect(const FramebufferConfig& config, const Point a, const Point b, const Color color) -> void {
    switch(config.pixel_format) {
    case PixelRGBResv8BitPerColor: {
        Framebuffer<PixelRGBResv8BitPerColor>(config).write_rect(a, b, color);
    }
    case PixelBGRResv8BitPerColor: {
        Framebuffer<PixelBGRResv8BitPerColor>(config).write_rect(a, b, color);
    }
    }
}

extern "C" void kernel_main(const FramebufferConfig& framebuffer_config) {
    const auto framebuffer = reinterpret_cast<uint8_t*>(framebuffer_config.frame_buffer);
    write_rect(framebuffer_config, {0, 0}, {framebuffer_config.horizontal_resolution, framebuffer_config.vertical_resolution}, 0xFF0000);
    while(1) {
        __asm__("hlt");
    }
}
