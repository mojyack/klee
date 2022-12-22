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

template <class T>
concept Number = std::integral<T> || std::is_pointer_v<T>;

template <Number T>
auto itos(const T value) -> std::array<char, sizeof(T) * 2 + 1> {
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

inline auto cursor = std::array<int, 2>{0, 0};

template <class T>
concept Printable = Number<T> || std::is_convertible_v<T, std::string_view>;

inline auto print(const std::string_view str) -> void {
    const auto font_size = get_font_size();
    draw_string(Point(cursor[0], cursor[1]), str.data());
    cursor[0] += font_size[0] * str.size();
}

inline auto print(const char* const str) -> void {
    print(std::string_view(str));
}

template <Number T>
auto print(T value) -> void {
    auto str = itos(value);
    print(std::string_view(str.data(), str.size()));
}

template <Printable Arg, Printable... Args>
auto print(Arg arg, Args... args) -> void {
    if constexpr(std::is_convertible_v<Arg, std::string_view>) {
        print(std::string_view(arg));
    } else {
        print(arg);
    }
    if constexpr(sizeof...(args) >= 1) {
        print(args...);
    }
}

template <Printable... Args>
auto println(Args... args) -> void {
    print(args...);

    constexpr auto line_width = 2;

    const auto size      = fb->get_size();
    const auto font_size = get_font_size();
    cursor[1] += font_size[1];
    if(cursor[1] + line_width >= size[1]) {
        cursor[1] = 0;
    }
    cursor[0] = 0;
    fb->write_rect(Point(0, cursor[1]), Point(size[0], cursor[1] + font_size[1]), RGBColor(0x000000));
    fb->write_rect(Point(0, cursor[1] + font_size[1]), Point(size[0], cursor[1] + font_size[1] + line_width), RGBColor(0xFFFFFF));
}
} // namespace debug
