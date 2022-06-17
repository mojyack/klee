#pragma once
#include <array>

#include "framebuffer.hpp"

class Console {
  private:
    constexpr static size_t max_rows    = 50;
    constexpr static size_t max_columns = 160;

    struct Line {
        std::array<char, max_columns> data;
        size_t                        len;
    };

    const FramebufferConfig&   framebuffer_config;
    std::array<Line, max_rows> buffer;

    uint32_t head    = 0;
    uint32_t tail    = 1;
    uint32_t row     = 0;
    uint32_t column  = 0;
    uint32_t rows    = 25;
    uint32_t columns = 80;

    auto newline() -> void {
        column = 0;
        tail   = (tail + 1) % buffer.size();
        if(row + 1 != rows) {
            row += 1;
        } else {
            head = (head + 1) % buffer.size();
        }
        buffer[tail == 0 ? buffer.size() - 1 : tail - 1].len = 0;

        const auto font_size = Framebuffer<PixelRGBResv8BitPerColor>::get_font_size();
        FRAMEBUFFER_INVOKE(write_rect, framebuffer_config, {0, 0}, Point(font_size[0] * columns, font_size[1] * rows), uint8_t(0x00));

        for(auto i = head, n = uint32_t(0); i != tail; i = (i + 1) % buffer.size(), n += 1) {
            const auto& l = buffer[i];
            FRAMEBUFFER_INVOKE(write_string, framebuffer_config, calc_position(n, 0), std::string_view(l.data.data(), l.len), uint8_t(0xFF));
        }
    }

    static auto calc_position(const uint32_t row, const uint32_t column) -> Point {
        constexpr auto font_size = Framebuffer<PixelRGBResv8BitPerColor>::get_font_size();
        return Point(font_size[0] * column, font_size[1] * row);
    }

  public:
    auto puts(const std::string_view str) -> void {
        for(const auto c : str) {
            if(c == '\n') {
                newline();
                continue;
            }
            auto& line        = buffer[(head + row) % buffer.size()];
            line.data[column] = c;
            line.len += 1;
            if(column + 1 != columns) {
                FRAMEBUFFER_INVOKE(write_ascii, framebuffer_config, calc_position(row, column), c, uint8_t(0xFF));
                column += 1;
            } else {
                newline();
            }
        }
    }

    Console(const FramebufferConfig& framebuffer_config) : framebuffer_config(framebuffer_config) {
        buffer[0].len = 0;
    }
};

inline auto console = (Console*)(nullptr);
