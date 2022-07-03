#pragma once
#include <array>
#include <span>
#include <string_view>

#include "stdio.h"

struct PrintBuffer {
    static constexpr auto buffer_size = 1024 * 4;

    std::array<char, buffer_size> buffer;
    size_t                        head = 0;
    size_t                        len  = 0;
};

inline auto printk_buffer = PrintBuffer();

inline auto printk(std::span<char> buf) -> int {
    const auto buf_len = buf.size();

    if(buf_len >= printk_buffer.buffer.size()) {
        printk_buffer.head = 0;
        printk_buffer.len  = printk_buffer.buffer.size();
        buf                = buf.subspan(buf.size() - printk_buffer.buffer.size());
        memcpy(printk_buffer.buffer.data(), buf.data(), printk_buffer.buffer.size());
        return buf_len;
    }

    auto       copy_left = buf_len;
    const auto copy_head = (printk_buffer.head + printk_buffer.len) % printk_buffer.buffer.size();
    printk_buffer.head   = printk_buffer.len == printk_buffer.buffer.size() ? printk_buffer.head + copy_left : printk_buffer.head;
    printk_buffer.len    = std::min(printk_buffer.len + buf_len, printk_buffer.buffer.size());
    {
        const auto buffer_left = printk_buffer.buffer.size() - copy_head;
        const auto len         = copy_left < buffer_left ? copy_left : buffer_left;
        memcpy(printk_buffer.buffer.data() + copy_head, buf.data(), len);
        buf = buf.subspan(len);
        copy_left -= len;
    }
    if(copy_left > 0) {
        printk_buffer.head = (printk_buffer.head + copy_left) % printk_buffer.buffer.size();
        memcpy(printk_buffer.buffer.data(), buf.data(), copy_left);
    }
    return buf_len;
}

inline auto printk(const char* const format, ...) -> int {
    static auto buffer = std::array<char, 1024>();

    va_list ap;
    va_start(ap, format);
    const auto result = vsnprintf(buffer.data(), buffer.size(), format, ap);
    va_end(ap);

    return printk(std::span<char>(buffer.data(), result));
}
