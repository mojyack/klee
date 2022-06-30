#pragma once
#include "framebuffer.hpp"
#include "window.hpp"

class Layer {
  private:
    std::vector<std::unique_ptr<Window>> windows;

    static auto min(const auto a, const auto b) {
        return a < b ? a : b;
    }

  public:
    template <WindowLike T, class... Args>
    auto open_window(Args&&... args) -> T* {
        const auto w = new T(std::move(args)...);
        windows.emplace_back(w);
        return w;
    }

    auto refresh() -> void {
        const auto fb_size = framebuffer->get_size();
        for(const auto& w : windows) {
            w->refresh_buffer();
            const auto  pos         = w->get_position();
            const auto  size        = w->get_size();
            const auto  y_begin     = pos.y >= 0 ? pos.y : 0;
            const auto  y_limit     = min(pos.y + size[1], 0 + fb_size[1]);
            const auto  y_src_offet = pos.y < 0 ? -pos.y : 0;
            const auto  x_begin     = pos.x >= 0 ? pos.x : 0;
            const auto  x_limit     = min(pos.x + size[0], 0 + fb_size[0]);
            const auto  x_src_offet = pos.x < 0 ? -pos.x : 0;
            const auto& buffer      = w->get_buffer();
            if(w->has_alpha()) {
                for(auto y = y_begin; y < y_limit; y += 1) {
                    const auto src_y = y - y_begin + y_src_offet;
                    for(auto x = x_begin; x < x_limit; x += 1) {
                        const auto src_x     = x - x_begin + x_src_offet;
                        const auto dst_color = RGBAColor(framebuffer->read_pixel({x, y}), RGBAColor::from_native);
                        const auto src_color = RGBAColor(buffer[src_y * size[0] + src_x], RGBAColor::from_native);
                        const auto opacity   = 1. * src_color.a / 0xFF;
                        const auto r         = src_color.r * opacity + dst_color.r * (1 - opacity);
                        const auto g         = src_color.g * opacity + dst_color.g * (1 - opacity);
                        const auto b         = src_color.b * opacity + dst_color.b * (1 - opacity);
                        framebuffer->write_pixel({x, y}, RGBColor(r, g, b));
                    }
                }
            } else {
                for(auto y = y_begin; y < y_limit; y += 1) {
                    const auto src_y = y - y_begin + y_src_offet;
                    framebuffer->copy_array(buffer.data() + src_y * size[0] + x_src_offet, {x_begin, y}, x_limit - x_begin);
                }
            }
        }
    }

    auto try_grub(const Point point) const -> Window* {
        for(auto& w : windows) {
            if(w->is_grabbable(point - w->get_position())) {
                return w.get();
            }
        }
        return nullptr;
    }

    auto focus(const Window* window) -> bool {
        for(auto i = windows.begin(); i < windows.end(); i += 1) {
            if(i->get() != window) {
                continue;
            }
            auto target = std::move(*i);
            windows.erase(i);
            windows.emplace_back(std::move(target));
            return true;
        }
        return false;
    }
};

class WindowManager {
  private:
    std::vector<Layer> layers;

  public:
    auto create_layer() -> size_t {
        auto id = layers.size();
        layers.emplace_back();
        return id;
    }

    auto get_layer(const size_t id) -> Layer& {
        return layers[id];
    }

    auto refresh() -> void {
        for(auto& l : layers) {
            l.refresh();
        }
    }

    auto refresh_layer(const size_t id) -> void {
        layers[id].refresh();
    }

    auto try_grub(const Point point) const -> Window* {
        for(auto& l : layers) {
            const auto w = l.try_grub(point);
            if(w != nullptr) {
                return w;
            }
        }
        return nullptr;
    }
};
