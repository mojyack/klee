#pragma once
#include "../fs/drivers/dev/keyboard.hpp"
#include "../message.hpp"

namespace devfs {
class USBKeyboard : public fs::dev::KeyboardDevice {
  public:
    auto push_packet(const fs::dev::KeyboardPacket packet) -> void {
        fs::dev::KeyboardDevice::push_packet(packet);
    }
};
}; // namespace devfs
