#pragma once
#include <string_view>
#include <string>

inline auto u16tou8(const std::u16string_view str) -> std::string {
    // TODO
    // proper encoding conversion
    auto buffer = std::string(str.size(), '\0');
    for(auto i = 0; i < str.size(); i += 1) {
        buffer[i] = str[i];
    }
    return buffer;
}
