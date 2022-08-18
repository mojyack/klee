#pragma once
#include "../message.hpp"
#include "../fs/drivers/dev.hpp"

namespace devfs {
class USBKeyboard : public fs::dev::KeyboardDevice {
  public:
    auto push_packet(const fs::dev::KeyboardPacket packet) -> void {
        fs::dev::KeyboardDevice::push_packet(packet);
    }
};
}; // namespace devfs
