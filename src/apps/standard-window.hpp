#pragma once
#include <string>

#include "../window.hpp"

class StandardWindow : public Window {
  private:
    static inline constexpr auto titlebar_height = 20;
    static inline constexpr auto border_pixel    = 1;
    static inline constexpr auto titlebar_color  = 0x2E3440FF;
    static inline constexpr auto border_color    = 0x4C566AFF;

    std::string title;

  protected:
    virtual auto update_contents(Point origin) -> void = 0;

  public:
    auto refresh_buffer() -> void final {
        const auto size = get_size();

        // draw border
        draw_rect({0, 0}, {size[0], border_pixel}, border_color);                            // top
        draw_rect({0, size[1] - border_pixel}, {size[0], size[1]}, border_color);            // bottom
        draw_rect({0, border_pixel}, {border_pixel, size[1]}, border_color);                 // left
        draw_rect({size[0] - border_pixel, border_pixel}, {size[0], size[1]}, border_color); // right

        // draw titlebar
        const auto font_size = get_font_size();
        draw_rect({border_pixel, border_pixel}, {size[0] - border_pixel, border_pixel + titlebar_height}, titlebar_color);
        draw_string(Point(border_pixel + (size[0] - font_size[0] * title.size()) / 2, border_pixel + (titlebar_height - get_font_size()[1]) / 2), title, 0xFFFFFFFF);

        // draw contents
        update_contents({border_pixel, border_pixel + titlebar_height});
    }

    auto is_grabbable(const Point point) const -> bool override {
        return Rectangle{{border_pixel, border_pixel}, {get_size()[0] - border_pixel, border_pixel + titlebar_height}}.contains(point);
    }

    StandardWindow(const int width, const int height, std::string title) : Window(width + border_pixel * 2, height + titlebar_height + border_pixel * 2),
                                                                           title(std::move(title)) {}
};
