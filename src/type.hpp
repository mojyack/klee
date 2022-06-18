#pragma once
#include <cstdint>

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
};

struct RGBColor {
    char r;
    char g;
    char b;

    RGBColor(const char r, const char g, const char b) : r(r), g(g), b(b) {}
    explicit RGBColor(const int32_t color) : r((color >> 16) & 0xFF), g((color >> 8) & 0xFF), b((color >> 0) & 0xFF) {}
};
