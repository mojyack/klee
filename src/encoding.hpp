#pragma once
#include <string_view>
#include <vector>

inline auto u16tou8(const std::u16string_view str) -> std::vector<char> {
    // TODO
    // proper encoding conversion
    auto buffer = std::vector<char>(str.size() / 2);
    for(auto i = 0; i < str.size(); i += 1) {
        buffer[i] = str[i];
    }
    return buffer;
}
