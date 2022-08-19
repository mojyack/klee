#pragma once
#include "fs/main.hpp"

#define handle_or(var, path, mode)                                  \
    auto var##_result = Result<fs::Handle>();                       \
    {                                                               \
        auto [lock, manager] = fs::manager->access();               \
        auto& root           = manager.get_fs_root();               \
        var##_result         = root.open(path, fs::OpenMode::mode); \
    }                                                               \
    if(!var##_result) {                                             \
        task::manager->get_current_task().exit();                   \
        return;                                                     \
    }                                                               \
    auto& var = var##_result.as_value();

#define exit_or(exp)                              \
    if(exp) {                                     \
        printk("terminal errror %d", exp.as_int()); \
        task::manager->get_current_task().exit(); \
    }

namespace terminal {
class FramebufferWriter {
  private:
    uint8_t*&             data;
    std::array<size_t, 2> size;

    auto find_pointer(const Point point) -> uint8_t* {
        return data + (point.y * size[0] + point.x) * 4;
    }

  public:
    auto write_pixel(const Point point, const RGBColor color) -> void {
        write_pixel(point, color.pack());
    }

    auto write_pixel(const Point point, const uint32_t color) -> void {
        *reinterpret_cast<uint32_t*>(find_pointer(point)) = color;
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
                auto p = reinterpret_cast<uint64_t*>(find_pointer({x, y}));
                *p     = c;
            }
        }
        if((b.x - a.x) % 2 != 0) {
            for(auto y = a.y; y < b.y; y += 1) {
                auto p = reinterpret_cast<uint64_t*>(find_pointer({1, y}));
                *p     = c;
            }
        }
    }

    auto write_rect(const Point a, const Point b, const uint8_t color) -> void {
        const auto c = uint32_t(color);
        write_rect(a, b, c | c << 8 | c << 16);
    }

    auto draw_ascii(const Point point, const char c, const RGBColor color) -> void {
        const auto font = get_font(c);
        for(auto y = 0; y < get_font_size()[1]; y += 1) {
            for(auto x = 0; x < get_font_size()[0]; x += 1) {
                if(!((font[y] << x) & 0x80u)) {
                    continue;
                }
                write_pixel({x + point.x, y + point.y}, color);
            }
        }
    }

    auto draw_string(const Point point, const std::string_view str, const RGBColor color) -> void {
        for(auto i = uint32_t(0); i < str.size(); i += 1) {
            draw_ascii(Point(point.x + get_font_size()[0] * i, point.y), str[i], color);
        }
    }

    FramebufferWriter(uint8_t*& data, const std::array<size_t, 2> size) : data(data), size(size) {}
};

class EventsWaiter {
  private:
    std::vector<uint64_t> event_ids;
    std::vector<Event*>   events;

  public:
    auto wait() -> size_t {
    loop:
        for(auto i = 0; i < events.size(); i += 1) {
            auto& e = *events[i];
            if(e.test()) {
                return i;
            }
        }

        task::manager->get_current_task().wait_events(event_ids);
        goto loop;
    }

    template <size_t size>
    EventsWaiter(std::array<fs::Handle*, size> handles) {
        event_ids.resize(handles.size());
        events.resize(handles.size());
        for(auto i = 0; i < handles.size(); i += 1) {
            auto& h      = handles[i];
            auto& e      = h->read_event();
            event_ids[i] = e.read_id();
            events[i]    = &e;
        }
    }
};

inline auto main(const uint64_t id, const int64_t data) -> void {
    // open devices
    handle_or(keyboard, "/dev/keyboard-usb0", Read);
    handle_or(framebuffer, "/dev/fb-uefi0", Write);

    // get framebuffer config
    auto fb_size = std::array<size_t, 2>();
    auto fb_data = (uint8_t**)(nullptr);
    exit_or(framebuffer.control_device(fs::DeviceOperation::GetSize, &fb_size));
    exit_or(framebuffer.control_device(fs::DeviceOperation::GetDirectPointer, &fb_data));

    auto fbwriter = FramebufferWriter(*fb_data, fb_size);

    auto buf          = fs::dev::KeyboardPacket();
    auto event_waiter = EventsWaiter(std::array{&keyboard, &framebuffer});
    auto swap_done    = true;
    auto swap_pending = false;
    while(true) {
        switch(event_waiter.wait()) {
        case 0: {
            if(!keyboard.read(0, sizeof(fs::dev::KeyboardPacket), &buf)) {
                printk("read error %d\n");
            }
            auto       str = std::array<char, 32>();
            const auto len = snprintf(str.data(), str.size(), "[%c] %02X %02X", buf.ascii, buf.keycode, buf.modifier);
            printk({str.data(), size_t(len)});
            fbwriter.write_rect({0, 0}, {int(get_font_size()[0] * len), get_font_size()[1]}, RGBColor(0));
            fbwriter.draw_string({0, 0}, {str.data(), size_t(len)}, 0xFFFFFFF);
            if(swap_done) {
                swap_done = false;
                exit_or(framebuffer.control_device(fs::DeviceOperation::Swap, 0))
            } else {
                swap_pending = true;
            }
        } break;
        case 1:
            if(swap_pending) {
                swap_pending = false;
                exit_or(framebuffer.control_device(fs::DeviceOperation::Swap, 0))
            } else {
                swap_done = true;
            }
            break;
        }
    }
    printk("terminal exit\n");

    task::manager->get_current_task().exit();
}
} // namespace terminal

#undef handle_or
#undef exit_or
