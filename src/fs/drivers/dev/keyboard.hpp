#pragma once
#include "base.hpp"

namespace fs::dev {
struct KeyboardPacket {
    uint8_t keycode;
    uint8_t modifier;
    char    ascii;
    uint8_t _padding;
};

static_assert(sizeof(KeyboardPacket) == 4);

class KeyboardDevice : public Device {
  private:
    Critical<std::vector<KeyboardPacket>> critical_packets;
    bool                                  active = false;
    Event                                 write_event;

  protected:
    auto push_packet(const KeyboardPacket packet) -> void {
        if(!active) {
            return;
        }

        {
            auto [lock, packets] = critical_packets.access();
            packets.emplace_back(packet);
        }
        write_event.notify();
    }

  public:
    auto read(uint64_t& handle_data, const size_t block, const size_t count, void* const buffer) -> Result<size_t> override {
        if(block != 0) {
            return Error::Code::IndexOutOfRange;
        }

        auto copy_num = size_t();
        while(true) {
            {
                auto [lock, packets] = critical_packets.access();
                copy_num             = std::min(count, packets.size());
            }

            if(copy_num != 0) {
                break;
            }
            write_event.wait();
            write_event.reset();
        }

        auto [lock, packets]  = critical_packets.access();
        const auto copy_bytes = sizeof(KeyboardPacket) * copy_num;
        memcpy(buffer, packets.data(), copy_bytes);
        packets.erase(packets.begin(), packets.begin() + copy_num);
        return size_t(copy_bytes);
    }

    auto on_handle_create(uint64_t& handle_data) -> void override {
        active = true;
    }

    auto on_handle_destroy(uint64_t& handle_data) -> void override {
        active = false;

        auto [lock, packets] = critical_packets.access();
        packets.clear();
    }

    // devfs specific
    auto get_filesize() const -> size_t override {
        auto [lock, packets] = critical_packets.access();
        return packets.size() * sizeof(KeyboardPacket);
    }

    auto get_device_type() const -> DeviceType override {
        return DeviceType::Keyboard;
    }

    auto get_attributes() const -> fs::Attributes override {
        return fs::Attributes{
            .read_level  = fs::OpenLevel::Single,
            .write_level = fs::OpenLevel::Block,
            .exclusive   = true,
            .volume_root = false,
            .cache       = false,
        };
    }

    auto get_blocksize_exp() const -> uint8_t override {
        return 2;
    }
};

} // namespace fs::dev
