#pragma once
#include <array>
#include <string_view>

#include "font.hpp"
#include "window.hpp"

class Console : public Window {
  private:
    constexpr static auto max_rows    = size_t(50);
    constexpr static auto max_columns = size_t(160);

    struct Line {
        std::array<char, max_columns> data;
        size_t                        len;
    };

    std::array<uint32_t, 2>    font_size;
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
            scroll(-font_size[1]);
            draw_rect(Point(0, (rows - 1) * font_size[1]), Point(font_size[0] * columns, font_size[1] * rows), 0x000000FF);
        }
        buffer[tail == 0 ? buffer.size() - 1 : tail - 1].len = 0;
    }

    auto calc_position(const uint32_t row, const uint32_t column) -> Point {
        return Point(font_size[0] * column, font_size[1] * row);
    }

  public:
    auto get_font_size() const -> std::array<uint32_t, 2> {
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

    Console(const int width, const int height) : font_size(::get_font_size()) {
        resize(height / font_size[1], width / font_size[0]);
    }
};

inline auto console = (Console*)(nullptr);
