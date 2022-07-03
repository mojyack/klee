#pragma once
#include "../window.hpp"

class WallpaperApp : public Window {
  private:
    auto fill_buffer() -> void {
    }

  public:
    auto refresh_buffer(bool focused) -> void override {
        const auto lock   = lock_window_resources();
        const auto [w, h] = get_size();
        for(auto y = 0; y < h; y += 1) {
            for(auto x = 0; x < w; x += 1) {
                const auto rate = 1. * y / h * x / w;
                auto       c    = RGBAColor(0xFF * rate, 0xFF * rate, 0xFF * rate, 0xFF);
                draw_pixel({x, y}, c);
            }
        }
    }

    auto is_grabbable(const Point point) const -> bool override {
        return false;
    }

    auto resize_window(const uint32_t width, const uint32_t height) -> void {
        Window::resize_window(width, height);
        fill_buffer();
    }

    WallpaperApp(const int width, const int height) : Window(width, height) {
        fill_buffer();
    }
};
