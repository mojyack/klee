#pragma once
#include <string>

#include "../window.hpp"

class StandardWindow : public Window {
  private:
    static inline constexpr auto titlebar_height = 20;
    static inline constexpr auto border_pixel    = 1;
    static inline constexpr auto text_color      = std::array{0xD8DEE9FFu, 0x4C566AFFu};
    static inline constexpr auto titlebar_color  = std::array{0x2E3440FFu, 0x202020FFu};
    static inline constexpr auto border_color    = std::array{0x4C566AFFu, 0x2E3440FFu};

    bool        last_focused;
    std::string title;

  protected:
    bool         dirty                                 = true;
    virtual auto update_contents(Point origin) -> void = 0;

    auto get_contents_size() -> std::array<int, 2> {
        const auto [w, h] = get_size();
        return {w - border_pixel * 2, h - titlebar_height - border_pixel * 2};
    }

    auto resize_window(const int new_width, const int new_height) -> void {
        Window::resize_window(new_width + border_pixel * 2, new_height + titlebar_height + border_pixel * 2);
        refresh();
    }

  public:
    auto refresh_buffer(const bool focused) -> void final {
        if(!dirty && focused == last_focused) {
            return;
        }

        const auto size = get_size();

        // draw border
        draw_rect({0, 0}, {size[0], border_pixel}, border_color[focused ? 0 : 1]);                            // top
        draw_rect({0, size[1] - border_pixel}, {size[0], size[1]}, border_color[focused ? 0 : 1]);            // bottom
        draw_rect({0, border_pixel}, {border_pixel, size[1]}, border_color[focused ? 0 : 1]);                 // left
        draw_rect({size[0] - border_pixel, border_pixel}, {size[0], size[1]}, border_color[focused ? 0 : 1]); // right

        // draw titlebar
        const auto font_size = get_font_size();
        draw_rect({border_pixel, border_pixel}, {size[0] - border_pixel, border_pixel + titlebar_height}, titlebar_color[focused ? 0 : 1]);
        draw_string(Point(border_pixel + (size[0] - font_size[0] * title.size()) / 2, border_pixel + (titlebar_height - get_font_size()[1]) / 2), title, text_color[focused ? 0 : 1]);

        last_focused = focused;

        // draw contents
        if(dirty) {
            update_contents({border_pixel, border_pixel + titlebar_height});
            dirty = false;
        }
    }

    auto is_grabbable(const Point point) const -> bool override {
        return Rectangle{{border_pixel, border_pixel}, {get_size()[0] - border_pixel, border_pixel + titlebar_height}}.contains(point);
    }

    StandardWindow(const int width, const int height, std::string title) : Window(width + border_pixel * 2, height + titlebar_height + border_pixel * 2),
                                                                           title(std::move(title)) {}
};
