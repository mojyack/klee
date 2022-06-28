#pragma once
#include <array>
#include <concepts>
#include <string_view>
#include <vector>

#include "error.hpp"
#include "font.hpp"
#include "framebuffer.hpp"
#include "type.hpp"

class Window {
  protected:
    enum class PositionConstraints {
        Free,
        WithinScreen,
    };

  private:
    std::array<int, 2>    size;
    std::vector<uint32_t> buffer;
    Point                 pos              = {0, 0};
    PositionConstraints   pos_constraint   = PositionConstraints::Free;
    bool                  is_alpha_enabled = false;

    auto fix_position() -> void {
        switch(pos_constraint) {
        case PositionConstraints::Free:
            break;
        case PositionConstraints::WithinScreen: {
            const auto size = framebuffer->get_size();
            if(pos.x < 0) {
                pos.x = 0;
            }
            if(pos.x >= size[0]) {
                pos.x = size[0] - 1;
            }
            if(pos.y < 0) {
                pos.y = 0;
            }
            if(pos.y >= size[1]) {
                pos.y = size[1] - 1;
            }
        } break;
        }
    }

  protected:
    auto set_position_constraint(const PositionConstraints new_constraint) -> void {
        pos_constraint = new_constraint;
    }

    auto resize_window(const int new_width, const int new_height) -> void {
        size[0] = new_width;
        size[1] = new_height;
        buffer.resize(new_width * new_height);
    }

    auto enable_alpha(const bool flag) -> void {
        is_alpha_enabled = flag;
    }

    auto draw_pixel(const Point pos, const RGBAColor color) -> Error {
        const auto i = pos.y * size[0] + pos.x;
        if(i >= buffer.size()) {
            return Error::Code::IndexOutOfRange;
        }

        buffer[i] = color.pack();
        return Error::Code::Success;
    }

    auto draw_rect(const Point a, const Point b, const RGBAColor color) -> void {
        for(auto y = a.y; y < b.y; y += 1) {
            for(auto x = a.x; x < b.x; x += 1) {
                draw_pixel({x, y}, color);
            }
        }
    }

    auto draw_ascii(const Point point, const char c, const RGBAColor color) -> void {
        const auto font = get_font(c);
        for(auto y = 0; y < get_font_size()[1]; y += 1) {
            for(auto x = 0; x < get_font_size()[0]; x += 1) {
                if(!((font[y] << x) & 0x80u)) {
                    continue;
                }
                draw_pixel({x + point.x, y + point.y}, color);
            }
        }
    }

    auto draw_string(const Point point, const std::string_view str, const RGBAColor color) -> void {
        for(auto i = uint32_t(0); i < str.size(); i += 1) {
            draw_ascii(Point(point.x + get_font_size()[0] * i, point.y), str[i], color);
        }
    }

    auto get_font_size() -> std::array<uint32_t, 2> {
        return ::get_font_size();
    }

    auto scroll(const int dy) -> Error {
        if(abs(dy) >= size[1]) {
            return Error::Code::IndexOutOfRange;
        }

        if(dy > 0) {
            for(auto y = size[1] - dy - 1; y >= 0; y -= 1) {
                const auto offset = y * size[0];
                memcpy(buffer.data() + offset, buffer.data() + offset + dy * size[0], size[0] * 4);
            }
        } else if(dy < 0) {
            for(auto y = -dy; y < size[1]; y += 1) {
                const auto offset = y * size[0];
                memcpy(buffer.data() + offset + dy * size[0], buffer.data() + offset, size[0] * 4);
            }
        }
        return Error::Code::Success;
    }

  public:
    virtual auto refresh_buffer() -> void                      = 0;
    virtual auto is_grabbable(const Point point) const -> bool = 0;

    auto set_position(const Point new_pos) -> void {
        pos = new_pos;
        fix_position();
    }

    auto move_position(const Point displacement) -> void {
        pos += displacement;
        fix_position();
    }

    auto get_position() const -> Point {
        return pos;
    }

    auto get_size() const -> const std::array<int, 2>& {
        return size;
    }

    auto get_buffer() const -> const std::vector<uint32_t> {
        return buffer;
    }

    auto has_alpha() const -> bool {
        return is_alpha_enabled;
    }

    auto operator=(const Window& o) -> Window& = delete;
    Window(const Window& o)                    = delete;
    Window()                                   = default;
    Window(const int width, const int height) : size{width, height}, buffer(width * height) {}

    virtual ~Window() = default;
};

template <class T>
concept WindowLike = std::derived_from<T, Window>;
