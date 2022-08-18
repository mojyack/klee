#pragma once
#include "../fs/drivers/dev.hpp"

namespace devfs {
class GOPFrameBuffer : public fs::dev::FramebufferDevice {
  private:
    FramebufferConfig    config;
    std::vector<uint8_t> backbuffer;

  public:
    auto swap() -> void override {
        memcpy(config.frame_buffer, backbuffer.data(), backbuffer.size());
        task::kernel_task->send_message(MessageType::RefreshScreenDone);
    }

    GOPFrameBuffer(const FramebufferConfig& config) {
        data = backbuffer.data();
        size = {config.horizontal_resolution, config.vertical_resolution};
    }
};
}; // namespace devfs
