#pragma once
#include "console.hpp"

inline auto printk(const char* const format, ...) -> int {
    static auto buffer = std::array<char, 1024>();

    va_list ap;
    va_start(ap, format);
    const auto result = vsnprintf(buffer.data(), buffer.size(), format, ap);
    va_end(ap);
    console->puts(buffer.data());
    return result;
}
