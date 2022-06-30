#pragma once
#include "../memory.hpp"
#include "hid-decl.hpp"

namespace usb {
class HIDKeyboardDriver : public HIDBaseDriver {
  private:
    using ObserverType = void(uint8_t modifier, uint8_t keycode);

    std::array<std::function<ObserverType>, 4> observers;

    auto notify_keypush(const uint8_t modifier, const uint8_t keycode) -> void {
        for(const auto& o : observers) {
            if(!o) {
                break;
            }
            o(modifier, keycode);
        }
    }

  public:
    static inline std::function<ObserverType> default_observer;

    auto operator new(const size_t size) -> void* {
        return allocate_memory<void>(sizeof(HIDKeyboardDriver), 0, 0);
    }

    auto operator delete(void* const ptr) noexcept -> void {
        deallocate_memory(ptr);
    }

    auto on_data_received() -> Error override {
        for(auto i = 2; i < 8; i += 1) {
            const auto key = get_buffer()[i];
            if(key == 0) {
                continue;
            }
            const auto& prev_buf = get_prev_buffer();
            if(std::find(prev_buf.begin() + 2, prev_buf.end(), key) != prev_buf.end()) {
                continue;
            }
            notify_keypush(get_buffer()[0], key);
        }
        return Error::Code::Success;
    }

    auto subscribe_keypush(const std::function<ObserverType> observer) -> void {
        for(auto& o : observers) {
            if(o) {
                continue;
            }
            o = observer;
            return;
        }
    }

    HIDKeyboardDriver(Device* const device, const int interface_index) : HIDBaseDriver(device, interface_index, 8) {}
};
} // namespace usb
