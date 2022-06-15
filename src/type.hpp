#pragma once
#include <cstdint>

struct Point {
    uint32_t x;
    uint32_t y;

    auto operator+(const Point& o) const -> Point {
        return {x + o.x, y + o.y};
    }
};

struct RGBColor {
    char r;
    char g;
    char b;

    RGBColor(const char r, const char g, const char b) : r(r), g(g), b(b) {}
    RGBColor(const int32_t color) : r((color >> 16) & 0xFF), g((color >> 8) & 0xFF), b((color >> 0) & 0xFF) {}
};
