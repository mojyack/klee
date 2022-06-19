#pragma once
#include <array>
#include <string_view>

#include "font.hpp"
#include "window.hpp"

class Console : public Window {
  private:
    constexpr static auto font_size   = std::array<uint32_t, 2>{8, 16};
    constexpr static auto max_rows    = size_t(50);
    constexpr static auto max_columns = size_t(160);

    struct Line {
        std::array<char, max_columns> data;
        size_t                        len;
    };

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

        draw_rect({0, 0}, Point(font_size[0] * columns, font_size[1] * rows), 0x000000FF);

        for(auto i = head, n = uint32_t(0); n == 0 || i != tail; i = (i + 1) % buffer.size(), n += 1) {
            const auto& l = buffer[i];
            draw_string(calc_position(n, 0), std::string_view(l.data.data(), l.len), 0xFFFFFFFF);
        }
    }

    auto draw_ascii(const Point point, const char c, const RGBAColor color) -> void {
        const auto font = get_font(c);
        for(auto y = 0; y < font_size[1]; y += 1) {
            for(auto x = 0; x < font_size[0]; x += 1) {
                if(!((font[y] << x) & 0x80u)) {
                    continue;
                }
                draw_pixel({x + point.x, y + point.y}, color);
            }
        }
    }

    auto draw_string(const Point point, const std::string_view str, const RGBAColor color) -> void {
        for(auto i = uint32_t(0); i < str.size(); i += 1) {
            draw_ascii(Point(point.x + font_size[0] * i, point.y), str[i], color);
        }
    }

    static auto calc_position(const uint32_t row, const uint32_t column) -> Point {
        return Point(font_size[0] * column, font_size[1] * row);
    }

  public:
    static constexpr auto get_font_size() -> std::array<uint32_t, 2> {
        return font_size;
    }

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
                draw_ascii(calc_position(row, column), c, 0xFFFFFFFF);
                column += 1;
            } else {
                newline();
            }
        }
    }

    auto resize(uint32_t new_rows, uint32_t new_columns) -> void {
        new_rows    = new_rows <= max_rows ? new_rows : max_rows;
        new_columns = new_columns <= max_columns ? new_columns : max_columns;

        // TODO: implement correct resizing
        head    = 0;
        tail    = 1;
        row     = 0;
        column  = 0;
        rows    = new_rows;
        columns = new_columns;
        for(auto& l : buffer) {
            l.len = 0;
        }

        resize_window(columns * font_size[0], rows * font_size[1]);
    }

    auto refresh_buffer() -> void override {}

    Console(const uint32_t rows, const uint32_t columns) {
        resize(rows, columns);
    }
};

inline auto console = (Console*)(nullptr);
