#pragma once
#include <cstdint>

extern const uint8_t font_start;
extern const uint8_t font_end;
extern const uint8_t font_limit;

inline auto get_font(const char c) -> const uint8_t* {
    const auto index = 16 * static_cast<unsigned int>(c);
    if(index >= reinterpret_cast<uintptr_t>(&font_limit)) {
        return nullptr;
    }
    return &font_start + index;
}
