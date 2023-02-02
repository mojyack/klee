#pragma once
#include "fs/manager.hpp"
#include "process/elf-startup.hpp"

namespace terminal {
class FramebufferWriter {
  private:
    uint8_t*              data;
    std::array<size_t, 2> size;

    auto find_pointer(const Point point) -> uint8_t* {
        return data + (point.y * size[0] + point.x) * 4;
    }

  public:
    auto get_size() const -> const std::array<size_t, 2>& {
        return size;
    }

    auto draw_pixel(const Point point, const RGBColor color) -> void {
        draw_pixel(point, color.pack());
    }

    auto draw_pixel(const Point point, const uint32_t color) -> void {
        *reinterpret_cast<uint32_t*>(find_pointer(point)) = color;
    }

    auto draw_pixel(const Point point, const uint8_t color) -> void {
        const auto c = uint32_t(color);
        draw_pixel(point, c | c << 8 | c << 16);
    }

    auto draw_rect(const Point a, const Point b, const uint32_t color) -> void {
        const auto c = uint64_t(color) | uint64_t(color) << 32;
        for(auto y = a.y; y < b.y; y += 1) {
            for(auto x = a.x; x + 1 < b.x; x += 2) {
                auto p = reinterpret_cast<uint64_t*>(find_pointer({x, y}));
                *p     = c;
            }
        }
        if((b.x - a.x) % 2 != 0) {
            for(auto y = a.y; y < b.y; y += 1) {
                auto p = reinterpret_cast<uint64_t*>(find_pointer({b.x - 1, y}));
                *p     = c;
            }
        }
    }

    auto draw_rect(const Point a, const Point b, const uint8_t color) -> void {
        const auto c = uint32_t(color);
        draw_rect(a, b, c | c << 8 | c << 16);
    }

    auto draw_ascii(const Point point, const char c, const RGBColor color) -> void {
        const auto font = get_font(c);
        for(auto y = 0; y < get_font_size()[1]; y += 1) {
            for(auto x = 0; x < get_font_size()[0]; x += 1) {
                if(!((font[y] << x) & 0x80u)) {
                    continue;
                }
                draw_pixel({x + point.x, y + point.y}, color);
            }
        }
    }

    auto draw_string(const Point point, const std::string_view str, const RGBColor color) -> void {
        for(auto i = uint32_t(0); i < str.size(); i += 1) {
            draw_ascii(Point(point.x + get_font_size()[0] * i, point.y), str[i], color);
        }
    }

    FramebufferWriter(uint8_t* const data, const std::array<size_t, 2> size) : data(data), size(size) {}
};

class Terminal {
  private:
    struct Line {
        std::vector<char> data;
        size_t            len;
    };

    std::array<uint32_t, 2> font_size;
    std::vector<Line>       buffer;
    FramebufferWriter       fb;
    Event&                  refresh;

    uint32_t head    = 0;
    uint32_t tail    = 1;
    uint32_t row     = 0;
    uint32_t column  = 0;
    uint32_t rows    = 25;
    uint32_t columns = 80;

    auto draw_all() -> void {
        const auto [w, h] = fb.get_size();
        fb.draw_rect({0, 0}, Point(w, h), uint32_t(0x000000));
        for(auto i = head, n = uint32_t(0); n == 0 || i != tail; i = (i + 1) % buffer.size(), n += 1) {
            const auto& line = buffer[i];
            for(auto c = 0; c < line.len; c += 1) {
                fb.draw_ascii(calc_position(n, c), line.data[c], 0xFFFFFF);
            }
        }
    }

    auto draw_cursor(const uint32_t row, const uint32_t column, const bool draw) -> void {
        const auto p = calc_position(row, column + 1);
        fb.draw_rect(p, p + Point(1, font_size[1]), uint32_t(draw ? 0xFFFFFF : 0x000000));
    }

    auto newline() -> void {
        auto redraw = false;

        tail = (tail + 1) % buffer.size();
        if(row + 1 != rows) {
            if(column != 0) {
                draw_cursor(row, column - 1, false);
            }
            row += 1;
        } else {
            head   = (head + 1) % buffer.size();
            redraw = true;
            draw_cursor(row, column, true);
        }
        column                                               = 0;
        buffer[tail == 0 ? buffer.size() - 1 : tail - 1].len = 0;
        if(redraw) {
            draw_all();
        }
    }

    auto calc_position(const uint32_t row, const uint32_t column) -> Point {
        return Point(font_size[0] * column, font_size[1] * row);
    }

  public:
    auto get_font_size() const -> std::array<uint32_t, 2> {
        return font_size;
    }

    auto putc(const char chr, const bool do_refresh = true) -> void {
        auto& line = buffer[(head + row) % buffer.size()];
        switch(chr) {
        case 0x0a:
            newline();
            break;
        case 0x08: { // backspace
            if(column == 0) {
                break;
            }
            column -= 1;
            line.len -= 1;
            draw_cursor(row, column, false);
            const auto p = calc_position(row, column);
            fb.draw_rect(p, p + Point(font_size[0], font_size[1]), uint32_t(0x000000));
            draw_cursor(row, column - 1, true);
            break;
        }
        default: {
            line.data[column] = chr;
            line.len += 1;
            if(column + 1 != columns) {
                if(column != 0) {
                    draw_cursor(row, column - 1, false);
                }
                fb.draw_ascii(calc_position(row, column), chr, 0xFFFFFF);
                draw_cursor(row, column, true);
                column += 1;
            } else {
                newline();
            }
        } break;
        }
        if(do_refresh) {
            refresh.notify();
        }
    }

    auto puts(const std::string_view str) -> void {
        for(const auto c : str) {
            putc(c, false);
        }
        refresh.notify();
    }

    auto resize(const uint32_t new_rows, const uint32_t new_columns) -> void {
        head    = 0;
        tail    = 1;
        row     = 0;
        column  = 0;
        rows    = new_rows;
        columns = new_columns;
        buffer.resize(rows);
        for(auto& l : buffer) {
            l.len = 0;
            l.data.resize(columns);
        }
    }

    Terminal(FramebufferWriter& fb, Event& refresh) : font_size(::get_font_size()), fb(fb), refresh(refresh) {
        const auto [w, h] = fb.get_size();
        resize(h / font_size[1], w / font_size[0]);
    }
};

class EventsWaiter {
  private:
    std::vector<process::EventID> event_ids;
    std::vector<Event*>           events;

  public:
    auto wait() -> size_t {
    loop:
        for(auto i = 0; i < events.size(); i += 1) {
            auto& e = *events[i];
            if(e.test()) {
                return i;
            }
        }

        if(const auto e = process::manager->wait_events(event_ids)) {
            logger(LogLevel::Error, "terminal: wait_events failed: %d\n", e.as_int());
        }
        goto loop;
    }

    template <size_t size>
    EventsWaiter(std::array<Event*, size> es) {
        event_ids.resize(es.size());
        events.resize(es.size());
        for(auto i = 0; i < es.size(); i += 1) {
            event_ids[i] = es[i]->read_id();
            events[i]    = es[i];
        }
    }

    ~EventsWaiter() {
        if(const auto e = process::manager->unwait_events(event_ids)) {
            logger(LogLevel::Error, "terminal: failed to cancel events: %d\n", e.as_int());
        }
    }
};

#define handle_or(path, mode)                                       \
    auto handle_result = Result<fs::Handle>();                      \
    {                                                               \
        auto [lock, manager] = fs::critical_manager->access();      \
        handle_result        = manager.open(path, mode);            \
    }                                                               \
    if(!handle_result) {                                            \
        print("open error: %d", handle_result.as_error().as_int()); \
        return true;                                                \
    }                                                               \
    auto& handle = handle_result.as_value();

class Shell {
  private:
    static constexpr auto prompt = "> ";

    Terminal&   term;
    std::string line_buffer;

    static auto split(const std::string_view str) -> std::vector<std::string_view> {
        auto       result = std::vector<std::string_view>();
        const auto len    = str.size();
        auto       qot    = '\0';
        auto       arglen = size_t();
        for(auto i = size_t(0); i < len; i += 1) {
            auto start = i;
            if(str[i] == '\"' || str[i] == '\'') {
                qot = str[i];
            }
            if(qot != '\0') {
                i += 1;
                start += 1;
                while(i < len && str[i] != qot) {
                    i += 1;
                }
                if(i < len) {
                    qot = '\0';
                }
                arglen = i - start;
            } else {
                while(i < len && str[i] != ' ') {
                    i += 1;
                }
                arglen = i - start;
            }
            result.emplace_back(str.data() + start, arglen);
        }
        // dynamic_assert(qot == '\0', "unclosed quotes");
        return result;
    }

    auto putc(const char chr) -> void {
        term.putc(chr);
    }

    auto puts(const std::string_view str) -> void {
        term.puts(str);
    }

    auto print(const char* const format, ...) -> int {
        static auto buffer = std::array<char, 1024>();

        va_list ap;
        va_start(ap, format);
        const auto result = vsnprintf(buffer.data(), buffer.size(), format, ap);
        va_end(ap);
        puts(std::string_view(buffer.data(), result));
        return result;
    }

    auto interpret(const std::string_view arg) -> bool {
        const auto argv = split(arg);
        if(argv.size() == 0) {
            return true;
        }
        if(argv[0] == "echo") {
            for(auto a = argv.begin() + 1; a != argv.end(); a += 1) {
                puts(*a);
            }
        } else if(argv[0] == "exit") {
            return false;
        } else if(argv[0] == "dmesg") {
            for(auto i = 0; i < printk_buffer.len; i += 1) {
                putc(printk_buffer.buffer[(i + printk_buffer.head) % printk_buffer.buffer.size()]);
            }
        } else if(argv[0] == "mount") {
            if(argv.size() == 1) {
                auto mounts = std::vector<std::array<std::string, 2>>();
                {
                    auto [lock, manager] = fs::critical_manager->access();
                    mounts               = manager.get_mounts();
                }
                if(mounts.empty()) {
                    puts("(no mounts)");
                    return true;
                }
                for(const auto& m : mounts) {
                    print("%s on \"%s\"\n", m[0].data(), m[1].data());
                }
            } else if(argv.size() == 3) {
                auto e = Error();
                {
                    auto [lock, manager] = fs::critical_manager->access();
                    e                    = manager.mount(argv[1], argv[2]);
                }
                if(e) {
                    print("mount error: %d\n", e.as_int());
                }
            } else {
                puts("usage: mount\n");
                puts("       mount DEVICE MOUNTPOINT\n");
            }
        } else if(argv[0] == "umount") {
            if(argv.size() != 2) {
                puts("usage: umount MOUNTPOINT");
                return true;
            }
            auto e = Error();
            {
                auto [lock, manager] = fs::critical_manager->access();
                e                    = manager.unmount(argv[1]);
            }
            if(e) {
                print("unmount error: %d\n", e.as_int());
            }
        } else if(argv[0] == "ls") {
            auto path = std::string_view("/");
            if(argv.size() == 2) {
                path = argv[1];
            }
            handle_or(path, fs::open_ro);
            for(auto i = 0;; i += 1) {
                const auto r = handle.readdir(i);
                if(!r) {
                    const auto e = r.as_error();
                    if(e != Error::Code::EndOfFile) {
                        print("readdir error: %d", e.as_int());
                    }
                    break;
                }
                auto& o = r.as_value();
                puts(o.name);
                putc('\n');
            }
        } else if(argv[0] == "mkdir") {
            if(argv.size() != 3) {
                puts("usage: mkdir DIR NAME");
                return true;
            }

            handle_or(argv[1], fs::open_rw);
            if(const auto e = handle.create(argv[2], fs::FileType::Directory)) {
                print("create error: %d\n", e.as_int());
            }
            return true;
        } else if(argv[0] == "cat") {
            if(argv.size() != 2) {
                puts("usage: cat FILE");
                return true;
            }
            handle_or(argv[1], fs::open_ro);
            const auto size_r = handle.get_filesize();
            if(!size_r) {
                print("failed to get filesize: %d\n", size_r.as_error().as_int());
                return true;
            }
            auto           size   = size_r.as_value();
            auto           offset = 0;
            constexpr auto chunk  = size_t(64);
            auto           buffer = std::array<char, chunk>();
            while(size > 0) {
                const auto read_size   = std::min(size, chunk);
                auto       read_result = size_t();
                if(const auto read = handle.read(offset, read_size, buffer.data()); !read) {
                    print("read error: %d\n", read.as_error().as_int());
                    return true;
                } else {
                    read_result = read.as_value();
                }

                for(auto& c : buffer) {
                    if(c <= 0x00 || c == 0x09) {
                        c = ' ';
                    }
                }
                size -= read_size;
                offset += read_size;
                puts({buffer.data(), read_result});
            }
        } else if(argv[0] == "run") {
            if(argv.size() != 2) {
                puts("usage: run FILE");
                return true;
            }
            handle_or(argv[1], fs::open_ro);
            const auto num_frames         = (handle.get_filesize() + bytes_per_frame - 1) / bytes_per_frame;
            auto       code_frames_result = allocator->allocate(num_frames);
            if(!code_frames_result) {
                print("failed to allocate frames for code: %d\n", code_frames_result.as_error());
                return true;
            }
            auto code_frames = std::unique_ptr<SmartFrameID>(new SmartFrameID(std::move(code_frames_result.as_value())));
            if(const auto read = handle.read(0, handle.get_filesize(), (*code_frames)->get_frame()); !read) {
                print("file read error: %d\n", read.as_error().as_int());
                return true;
            }

            const auto pid   = process::manager->create_process();
            const auto tid_r = process::manager->create_thread(pid, process::elf_startup, reinterpret_cast<uint64_t>(code_frames.release()));
            if(!tid_r) {
                print("failed to create new thread: %d\n", tid_r.as_error().as_int());
            } else {
                const auto tid = tid_r.as_value();
                if(const auto e = process::manager->wakeup_thread(pid, tid)) {
                    print("failed to wakeup thread: %d\n", e.as_int());
                } else {
                    if(const auto e = process::manager->wait_thread(pid, tid)) {
                        print("failed to wait thread: %d\n", e.as_int());
                    }
                }
                if(const auto e = process::manager->wait_process(pid)) {
                    print("failed to wait process: %d\n", e.as_int());
                }
            }
        } else {
            puts("unknown command");
        }
        return true;
    }

  public:
    auto input(const char c) -> bool {
        switch(c) {
        case 0x0a:
            putc(c);
            if(!interpret(line_buffer)) {
                return false;
            }
            putc('\n');
            puts(prompt);
            line_buffer.clear();
            break;
        case 0x08:
            if(line_buffer.size() >= 1) {
                line_buffer.pop_back();
                putc(c);
            }
            break;
        default:
            line_buffer += c;
            putc(c);
            break;
        }
        return true;
    }

    Shell(Terminal& term) : term(term) {
        puts(prompt);
    }
};

#undef handle_or

#define handle_or(var, path, mode)                                      \
    auto var##_result = Result<fs::Handle>();                           \
    {                                                                   \
        auto [lock, manager] = fs::critical_manager->access();          \
        var##_result         = manager.open(path, mode);                \
    }                                                                   \
    if(!var##_result) {                                                 \
        printk("terminal: handle error %d\n", var##_result.as_error()); \
        process::manager->exit_this_thread();                           \
    }                                                                   \
    auto& var = var##_result.as_value();

#define return_or(exp) \
    if(exp) {          \
        return;        \
    }

struct ShellMainArg {
    Shell&      shell;
    fs::Handle& keyboard;
    Event&      exit;
};

inline auto shell_main(const uint64_t id, const int64_t data) -> void {
    auto& arg      = *reinterpret_cast<ShellMainArg*>(data);
    auto& shell    = arg.shell;
    auto& keyboard = arg.keyboard;
    auto& exit     = arg.exit;

    auto buf = fs::dev::KeyboardPacket();
    while(keyboard.read(0, sizeof(fs::dev::KeyboardPacket), &buf)) {
        if(!shell.input(buf.ascii)) {
            break;
        }
    }
    exit.notify();
    process::manager->exit_this_thread();
}

[[noreturn]] inline auto panic(const char* const message, const int code) -> void {
    logger(LogLevel::Error, "terminal: %s: %d\n", code);
    debug::println("terminal: error");
    while(true) {
        process::manager->sleep_this_thread();
    }
}

inline auto terminal_main(const char** const fb_device) -> void {
    // open devices
    handle_or(keyboard, "/dev/keyboard-usb0", fs::open_ro);
    handle_or(framebuffer, *fb_device, fs::open_wo);

    // get framebuffer config
    auto fb_size            = std::array<size_t, 2>();
    auto fb_data            = (uint8_t**)(nullptr);
    auto fb_double_buffered = false;
    auto fb_buffer          = std::vector<uint8_t>();
    return_or(framebuffer.control_device(fs::DeviceOperation::GetSize, &fb_size));
    return_or(framebuffer.control_device(fs::DeviceOperation::GetDirectPointer, &fb_data));
    return_or(framebuffer.control_device(fs::DeviceOperation::IsDoubleBuffered, &fb_double_buffered));
    if(fb_double_buffered) {
        fb_buffer = std::vector<uint8_t>(fb_size[0] * fb_size[1] * 4);
    }

    auto refresh    = Event();
    auto fbwriter   = FramebufferWriter(fb_double_buffered ? fb_buffer.data() : *fb_data, fb_size);
    auto term       = Terminal(fbwriter, refresh);
    auto shell      = Shell(term);
    auto shell_exit = Event();
    auto shell_arg  = ShellMainArg{shell, keyboard, shell_exit};

    const auto this_process   = process::manager->get_this_thread()->process;
    const auto shell_thread_r = process::manager->create_thread(this_process->id, shell_main, reinterpret_cast<int64_t>(&shell_arg));
    if(!shell_thread_r) {
        panic("failed to create shell thread", shell_thread_r.as_error());
    }
    const auto shell_thread = shell_thread_r.as_value();
    if(const auto e = process::manager->wakeup_thread(this_process->id, shell_thread)) {
        panic("failed to wakeup shell thread", e.as_int());
    }

    {
        auto event_waiter = EventsWaiter(std::array{&refresh, &framebuffer.read_event(), &shell_exit});
        auto swap_done    = true;
        auto swap_pending = false;
        auto exit         = false;
        while(!exit) {
            switch(event_waiter.wait()) {
            case 0: {
                refresh.reset();
                if(swap_done) {
                    swap_done = false;
                    if(fb_double_buffered) {
                        memcpy(*fb_data, fb_buffer.data(), fb_buffer.size());
                    }
                    return_or(framebuffer.control_device(fs::DeviceOperation::Swap, 0));
                } else {
                    swap_pending = true;
                }
            } break;
            case 1:
                framebuffer.read_event().reset();
                if(swap_pending) {
                    swap_pending = false;
                    if(fb_double_buffered) {
                        memcpy(*fb_data, fb_buffer.data(), fb_buffer.size());
                    }
                    return_or(framebuffer.control_device(fs::DeviceOperation::Swap, 0));
                } else {
                    swap_done = true;
                }
                break;
            case 2:
                exit = true;
                break;
            }
        }
    }

    if(const auto e = process::manager->wait_thread(this_process->id, shell_thread)) {
        panic("failed to wait for shell thread", e.as_int());
    }
}

inline auto main(const uint64_t id, const int64_t data) -> void {
    while(true) {
        terminal_main(reinterpret_cast<const char**>(data));
    }
}
} // namespace terminal

#undef handle_or
#undef exit_or
