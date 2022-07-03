#pragma once
#include <array>
#include <concepts>
#include <string_view>
#include <vector>

#include "../framebuffer.hpp"
#include "../type.hpp"
#include "framebuffer.h"

namespace uefi {
class Framebuffer : public ::Framebuffer {
  private:
    FramebufferConfig config;

    std::vector<uint8_t> backbuffer;

    auto find_pointer(const Point point, const bool flip) -> uint8_t* override {
        return backbuffer.data() + (point.y * config.pixels_per_scan_line + point.x) * 4;
    }

    auto do_swap(const bool flip) -> bool override {
        memcpy(config.frame_buffer, backbuffer.data(), backbuffer.size());
        notify_refresh_done();
        return true;
    }

  public:
    auto get_size() const -> std::array<size_t, 2> override {
        return {config.horizontal_resolution, config.vertical_resolution};
    }

    Framebuffer(const FramebufferConfig& config) : config(config),
                                                   backbuffer(config.horizontal_resolution * config.vertical_resolution * 4) {}
};
} // namespace uefi
