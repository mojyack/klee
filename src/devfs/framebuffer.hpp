#pragma once
#include "../fs/drivers/dev.hpp"

namespace devfs {
class GOPFrameBuffer : public fs::dev::FramebufferDevice {
  private:
    uint8_t*             gop_framebuffer;
    std::vector<uint8_t> backbuffer;

  public:
    auto swap() -> void override {
        memcpy(gop_framebuffer, backbuffer.data(), backbuffer.size());
        write_event->notify();
    }

    auto is_double_buffered() const -> bool override {
        return false;
    }

    GOPFrameBuffer(const FramebufferConfig& config) : gop_framebuffer(config.frame_buffer),
                                                      backbuffer(config.horizontal_resolution * config.vertical_resolution * 4) {
        data        = backbuffer.data();
        buffer_size = {config.horizontal_resolution, config.vertical_resolution};
    }
};
}; // namespace devfs
