#pragma once
#include <cstdint>

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
