#pragma once
#include <cstdint>

#define COLOR_RGB 1

struct Point {
    int x;
    int y;

    auto operator+(const Point& o) const -> Point {
        return {x + o.x, y + o.y};
    }

    auto operator+=(const Point& o) -> Point& {
        *this = *this + o;
        return *this;
    }

    auto operator-(const Point& o) const -> Point {
        return {x - o.x, y - o.y};
    }

    auto operator-=(const Point& o) -> Point& {
        *this = *this - o;
        return *this;
    }

    Point(const int x, const int y) : x(x), y(y) {}
};

struct Rectangle {
    Point a;
    Point b;

    auto width() const -> int {
        return b.x - a.x;
    }

    auto hight() const -> int {
        return b.y - a.y;
    }

    auto contains(const Point p) const -> bool {
        return a.x <= p.x && b.x > p.x && a.y <= p.y && b.y > p.y;
    }
};

struct RGBColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;

    RGBColor(const uint8_t r, const uint8_t g, const uint8_t b) : r(r), g(g), b(b) {}
    RGBColor(const int32_t color) : r((color >> 16) & 0xFF), g((color >> 8) & 0xFF), b((color >> 0) & 0xFF) {}
};

struct RGBAColor {
    using FromNative = int;

    static constexpr auto from_native = FromNative(0);

    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;

    auto pack() const -> uint32_t {
#if COLOR_RGB == 1
        return a << 24 | r << 16 | g << 8 | b;
#else
        return a << 24 | b << 16 | g << 8 | r;
#endif
    }

    RGBAColor(const uint8_t r, const uint8_t g, const uint8_t b, const uint8_t a) : r(r), g(g), b(b), a(a) {}

    RGBAColor(uint32_t color) : r((color >> 24) & 0xFF),
                                g((color >> 16) & 0xFF),
                                b((color >> 8) & 0xFF),
                                a((color >> 0) & 0xFF) {}

    RGBAColor(const uint32_t color, FromNative) {
        a = (color >> 24) & 0x000000FF;
        g = (color >> 8) & 0x000000FF;
#if COLOR_RGB == 1
        r = (color >> 16) & 0x000000FF;
        b = (color >> 0) & 0x000000FF;
#else
        b = (color >> 16) & 0x000000FF;
        r = (color >> 0) & 0x000000FF;
#endif
    }
};
