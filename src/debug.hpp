#pragma once
#include <string_view>

#include "font.hpp"
#include "type.hpp"
#include "uefi/framebuffer.h"

namespace debug {
class Framebuffer {
  private:
    FramebufferConfig config;

    auto find_pointer(const Point point) -> uint8_t* {
        return config.frame_buffer + (point.y * config.pixels_per_scan_line + point.x) * 4;
    }

  public:
    auto get_size() const -> std::array<size_t, 2> {
        return {config.horizontal_resolution, config.vertical_resolution};
    }

    auto write_pixel(const Point point, const RGBColor color) -> void {
        write_pixel(point, color.pack());
    }

    auto write_pixel(const Point point, const uint32_t color) -> void {
        const auto p                    = find_pointer(point);
        *reinterpret_cast<uint32_t*>(p) = color;
    }

    auto write_pixel(const Point point, const uint8_t color) -> void {
        const auto c = uint32_t(color);
        write_pixel(point, c | c << 8 | c << 16);
    }

    auto write_rect(const Point a, const Point b, const RGBColor color) -> void {
        write_rect(a, b, color.pack());
    }

    auto write_rect(const Point a, const Point b, const uint32_t color) -> void {
        const auto c = uint64_t(color) | uint64_t(color) << 32;
        for(auto y = a.y; y < b.y; y += 1) {
            for(auto x = a.x; x + 1 < b.x; x += 2) {
                auto p = reinterpret_cast<uint64_t*>(find_pointer({x, y}));
                *p     = c;
            }
        }
        if((b.x - a.x) % 2 != 0) {
            for(auto y = a.y; y < b.y; y += 1) {
                auto p = reinterpret_cast<uint64_t*>(find_pointer({1, y}));
                *p     = c;
            }
        }
    }

    auto write_rect(const Point a, const Point b, const uint8_t color) -> void {
        const auto c = uint32_t(color);
        write_rect(a, b, c | c << 8 | c << 16);
    }

    auto read_pixel(const Point point) -> uint32_t {
        return *reinterpret_cast<uint32_t*>(find_pointer(point));
    }

    auto copy_array(const uint32_t* const source, const Point dest, const size_t len) -> void {
        memcpy(find_pointer(dest), source, len * 4);
    }

    Framebuffer(const FramebufferConfig& config) : config(config) {}

    virtual ~Framebuffer() {}
};

inline auto fb = (Framebuffer*)(nullptr);

inline auto draw_ascii(const Point point, const char c) -> void {
    if(fb == nullptr) {
        return;
    }

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

inline auto cursor = std::array<int, 2>{0, 0};

struct Number {
    size_t   data;
    uint32_t base;
    uint32_t fill;

    Number(const auto data, const uint32_t base = 16, const uint32_t fill = 16) : data(static_cast<size_t>(data)),
                                                                                  base(base),
                                                                                  fill(fill) {}
};

template <class T>
concept NumberLike = std::is_constructible_v<Number, T>;

inline auto print(const char c) -> void {
    const auto font_size = get_font_size();
    draw_ascii(Point(cursor[0], cursor[1]), c);
    cursor[0] += font_size[0];
}

inline auto print(const std::string_view str) -> void {
    const auto font_size = get_font_size();
    draw_string(Point(cursor[0], cursor[1]), str.data());
    cursor[0] += font_size[0] * str.size();
}

inline auto print(const char* str) -> void {
    print(std::string_view(str));
}

inline auto print(const Number number) -> void {
    if(number.data == 0) {
        const auto fill = number.fill == 0 ? 1 : number.fill;
        for(auto i = 0; i < fill; i += 1) {
            print('0');
        }
        return;
    }

    auto buf = std::array<char, sizeof(size_t) * 8>();
    auto num = number.data;
    auto i   = -1;
    while(num != 0) {
        i += 1;
        const auto mod = num % number.base;
        buf[i]         = mod >= 10 ? (mod - 10) + 'a' : mod + '0';
        num /= number.base;
    }
    while((i + 1) < number.fill) {
        i += 1;
        buf[i] = '0';
    }
    while(i >= 0) {
        print(buf[i]);
        i -= 1;
    }
}

template <NumberLike T>
auto print(const T number) -> void {
    print(Number(number));
}

template <class T>
concept Printable = std::is_same_v<T, Number> ||
                    std::is_same_v<T, char> ||
                    std::is_same_v<T, std::string_view> ||
                    std::is_same_v<T, const char*> ||
                    NumberLike<T>;

template <Printable Arg, Printable... Args>
auto print(const Arg arg, const Args... args) -> void {
    print(arg);
    print(args...);
}

template <Printable... Args>
auto println(const Args... args) -> void {
    constexpr auto line_width = 2;

    const auto size      = fb->get_size();
    const auto font_size = get_font_size();
    fb->write_rect(Point(0, cursor[1]), Point(size[0], cursor[1] + font_size[1]), RGBColor(0x000000));
    fb->write_rect(Point(0, cursor[1] + font_size[1]), Point(size[0], cursor[1] + font_size[1] + line_width), RGBColor(0xFFFFFF));

    print(args...);

    cursor[1] += font_size[1];
    if(cursor[1] + line_width >= size[1]) {
        cursor[1] = 0;
    }
    cursor[0] = 0;
}
} // namespace debug
