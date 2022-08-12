#pragma once
#include <array>

#include "type.hpp"
#include "task/manager.hpp"

class Framebuffer {
  private:
    bool flip = false;

  protected:
    virtual auto find_pointer(Point point, bool flip) -> uint8_t* = 0;
    virtual auto do_swap(bool flip) -> bool                       = 0;

    auto notify_refresh_done() -> void {
       task::kernel_task->send_message(MessageType::RefreshScreenDone);
    }

  public:
    virtual auto get_size() const -> std::array<size_t, 2> = 0;

    auto write_pixel(const Point point, const RGBColor color) -> void {
        write_pixel(point, color.pack());
    }

    auto write_pixel(const Point point, const uint32_t color) -> void {
        const auto p                    = find_pointer(point, flip);
        *reinterpret_cast<uint32_t*>(p) = color;
    }

    auto write_pixel(const Point point, const uint8_t color) -> void {
        const auto c = uint32_t(color);
        write_pixel(point, c | c << 8 | c << 16);
    }

    auto write_rect(const Point a, const Point b, const RGBColor color) -> void {
        write_rect(a, b, color.pack());
    }

    auto write_rect(const Point a, const Point b, const uint32_t color) -> void {
        const auto c = uint64_t(color) | uint64_t(color) << 32;
        for(auto y = a.y; y < b.y; y += 1) {
            for(auto x = a.x; x + 1 < b.x; x += 2) {
                auto p = reinterpret_cast<uint64_t*>(find_pointer({x, y}, flip));
                *p     = c;
            }
        }
        if((b.x - a.x) % 2 != 0) {
            for(auto y = a.y; y < b.y; y += 1) {
                auto p = reinterpret_cast<uint64_t*>(find_pointer({1, y}, flip));
                *p     = c;
            }
        }
    }

    auto write_rect(const Point a, const Point b, const uint8_t color) -> void {
        const auto c = uint32_t(color);
        write_rect(a, b, c | c << 8 | c << 16);
    }

    auto read_pixel(const Point point) -> uint32_t {
        return *reinterpret_cast<uint32_t*>(find_pointer(point, flip));
    }

    auto copy_array(const uint32_t* const source, const Point dest, const size_t len) -> void {
        memcpy(find_pointer(dest, flip), source, len * 4);
    }

    auto swap() -> void {
        if(!do_swap(flip)) {
            return;
        }
        flip = !flip;
    };

    virtual ~Framebuffer() {}
};

inline auto framebuffer = (Framebuffer*)(nullptr);
