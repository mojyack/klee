#pragma once
#include "standard-window.hpp"

class CounterApp : public StandardWindow {
  private:
    uint64_t count = 0;

    auto update_contents(const Point origin) -> void override {
        auto       buffer = std::array<char, 32>();
        const auto n      = snprintf(buffer.data(), buffer.size(), "count: %lu", count);
        draw_rect(origin, origin + Point(300, 100), 0x303030FF);
        draw_string(origin + Point((300 - n * get_font_size()[0]) / 2, (100 - get_font_size()[1]) / 2), buffer.data(), 0xFFFFFFFF);
    }

  public:
    auto increment() -> void {
        dirty = true;
        count += 1;
    }

    CounterApp() : StandardWindow(300, 100, "counter") {}
};
