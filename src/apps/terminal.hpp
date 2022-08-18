#pragma once
#include "../fs/main.hpp"
#include "../mutex.hpp"
#include "../task/elf-startup.hpp"
#include "../util/variant.hpp"
#include "../window-manager.hpp"
#include "standard-window.hpp"

#include "../debug.hpp"

namespace terminal {
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

#define handle_or(path)                                             \
    auto handle_result = Result<fs::Handle>();                      \
    {                                                               \
        auto [lock, manager] = fs::manager->access();               \
        auto& root           = manager.get_fs_root();               \
        handle_result        = root.open(path, fs::OpenMode::Read); \
    }                                                               \
    if(!handle_result) {                                            \
        print("open error: %d", handle_result.as_error().as_int()); \
        return;                                                     \
    }                                                               \
    auto& handle = handle_result.as_value();

class Shell {
  private:
    static constexpr auto                 prompt = "> ";
    std::function<void(char)>             putc;
    std::function<void(std::string_view)> puts;

    std::string line_buffer;

    auto print(const char* const format, ...) -> int {
        static auto buffer = std::array<char, 1024>();

        va_list ap;
        va_start(ap, format);
        const auto result = vsnprintf(buffer.data(), buffer.size(), format, ap);
        va_end(ap);
        puts(std::string_view(buffer.data(), result));
        return result;
    }

    auto close_handle(fs::Handle handle) -> Error {
        auto [lock, manager] = fs::manager->access();
        auto& root           = manager.get_fs_root();
        return root.close(std::move(handle));
    }

    auto interpret(const std::string_view arg) -> void {
        const auto argv = split(arg);
        if(argv.size() == 0) {
            return;
        }
        if(argv[0] == "echo") {
            for(auto a = argv.begin() + 1; a != argv.end(); a += 1) {
                puts(*a);
            }
        } else if(argv[0] == "dmesg") {
            for(auto i = 0; i < printk_buffer.len; i += 1) {
                putc(printk_buffer.buffer[(i + printk_buffer.head) % printk_buffer.buffer.size()]);
            }
        } else if(argv[0] == "lsblk") {
            auto disks = std::vector<std::string>();
            {
                auto [lock, manager] = fs::manager->access();
                disks                = manager.list_block_devices();
            }
            for(const auto& d : disks) {
                puts(d.data());
            }
        } else if(argv[0] == "mount") {
            if(argv.size() == 1) {
                auto mounts = std::vector<fs::MountRecord>();
                {
                    auto [lock, manager] = fs::manager->access();
                    mounts               = manager.get_mounts();
                }
                if(mounts.empty()) {
                    puts("(no mounts)");
                    return;
                }
                for(const auto& m : mounts) {
                    print("%s on \"%s\"\n", fs::mdev_to_str(m.device).data(), m.path.data());
                }
            } else if(argv.size() == 3) {
                auto e = Error();
                {
                    auto [lock, manager] = fs::manager->access();
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
                return;
            }
            auto e = Error();
            {
                auto [lock, manager] = fs::manager->access();
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
            handle_or(path);
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
            close_handle(std::move(handle));
        } else if(argv[0] == "cat") {
            if(argv.size() != 2) {
                puts("usage: cat FILE");
                return;
            }
            handle_or(argv[1]);
            for(auto i = size_t(0); i < handle.get_filesize(); i += 1) {
                auto c = char();
                if(const auto read = handle.read(i, 1, &c); !read) {
                    print("read error: %d\n", read.as_error().as_int());
                    close_handle(std::move(handle));
                    return;
                }
                if(c >= 0x20) {
                    putc(c);
                    continue;
                }
                if(c <= 0) {
                    putc(' ');
                    continue;
                }
                switch(c) {
                case 0x09:
                    putc(' ');
                    break;
                case 0x0A:
                    putc('\n');
                    break;
                }
            }
            close_handle(std::move(handle));
        } else if(argv[0] == "run") {
            if(argv.size() != 2) {
                puts("usage: run FILE");
                return;
            }
            handle_or(argv[1]);
            const auto num_frames         = (handle.get_filesize() + bytes_per_frame - 1) / bytes_per_frame;
            auto       code_frames_result = allocator->allocate(num_frames);
            if(!code_frames_result) {
                print("failed to allocate frames for code: %d\n", code_frames_result.as_error());
                close_handle(std::move(handle));
                return;
            }
            auto code_frames = std::unique_ptr<SmartFrameID>(new SmartFrameID(code_frames_result.as_value(), num_frames));
            if(const auto read = handle.read(0, handle.get_filesize(), (*code_frames)->get_frame()); !read) {
                print("file read error: %d\n", read.as_error().as_int());
                close_handle(std::move(handle));
                return;
            }

            auto task = (task::Task*)(nullptr);
            {
                task = &task::task_manager->new_task();
                task->init_context(task::elf_startup, reinterpret_cast<uint64_t>(code_frames.get()));
                [[maybe_unused]] const auto raw_ptr = code_frames.release();
                task->wakeup();
            }
            close_handle(std::move(handle));
            task::task_manager->wait_task(task);
        } else {
            puts("unknown command");
        }
    }

  public:
    auto input(const char c) -> void {
        switch(c) {
        case 0x0a:
            putc(c);
            interpret(line_buffer);
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
    }

    Shell(const std::function<void(char)> putc, const std::function<void(std::string_view)> puts) : putc(putc), puts(puts) {
        puts(prompt);
    }
};

#undef handle_or
} // namespace terminal

class Terminal : public StandardWindow {
  private:
    struct Line {
        std::vector<char> data;
        size_t            len;
    };

    struct DrawOp {
        struct Rect {
            Rectangle rect;
            uint32_t  color;
        };

        struct Scroll {};

        struct Ascii {
            uint32_t row;
            uint32_t column;
            uint32_t color;
            char     ascii;
        };

        using Variant = Variant<Rect, Scroll, Ascii>;
    };

    std::array<uint32_t, 2> font_size;
    std::vector<Line>       buffer;

    uint32_t head    = 0;
    uint32_t tail    = 1;
    uint32_t row     = 0;
    uint32_t column  = 0;
    uint32_t rows    = 25;
    uint32_t columns = 80;

    Critical<std::vector<DrawOp::Variant>> draw_queue;

    auto enqueue_draw(auto&& args) -> void {
        auto [l, v] = draw_queue.access();
        v.emplace_back(std::move(args));
        dirty = true;
        refresh();
    }

    auto newline() -> void {
        tail = (tail + 1) % buffer.size();
        if(row + 1 != rows) {
            if(column != 0) {
                draw_cursor(row, column - 1, false);
            }
            row += 1;
        } else {
            head = (head + 1) % buffer.size();
            enqueue_draw(DrawOp::Scroll{});
            draw_cursor(row, column, true);
        }
        column                                               = 0;
        buffer[tail == 0 ? buffer.size() - 1 : tail - 1].len = 0;
    }

    auto calc_position(const uint32_t row, const uint32_t column) -> Point {
        return Point(font_size[0] * column, font_size[1] * row);
    }

    auto draw_cursor(const uint32_t row, const uint32_t column, const bool draw) -> void {
        const auto p = calc_position(row, column + 1);
        enqueue_draw(DrawOp::Rect{{p, p + Point(1, font_size[1])}, draw ? 0xFFFFFFFF : 0x000000FF});
    }

  public:
    auto get_font_size() const -> std::array<uint32_t, 2> {
        return font_size;
    }

    auto putc(const char chr) -> void {
        auto& line = buffer[(head + row) % buffer.size()];
        switch(chr) {
        case 0x0a:
            newline();
            return;
        case 0x08: // backspace
            if(column == 0) {
                return;
            }
            column -= 1;
            line.len -= 1;
            draw_cursor(row, column, false);
            const auto p = calc_position(row, column);
            enqueue_draw(DrawOp::Rect{{p, p + Point(font_size[0], font_size[1])}, 0x000000FF});
            draw_cursor(row, column - 1, true);
            return;
        }
        line.data[column] = chr;
        line.len += 1;
        if(column + 1 != columns) {
            if(column != 0) {
                draw_cursor(row, column - 1, false);
            }
            enqueue_draw(DrawOp::Ascii{row, column, 0xFFFFFFFF, chr});
            draw_cursor(row, column, true);
            column += 1;
        } else {
            newline();
        }
    }

    auto puts(const std::string_view str) -> void {
        for(const auto c : str) {
            putc(c);
        }
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

        const auto lock = lock_window_resources();
        resize_window(columns * font_size[0], rows * font_size[1]);
    }

    auto update_contents(const Point origin) -> void override {
        const auto lock = lock_window_resources();
        auto       q    = std::move(draw_queue.access().second);
        while(!q.empty()) {
            for(const auto& o : q) {
                using V = DrawOp::Variant;
                switch(o.index()) {
                case V::index_of<DrawOp::Rect>(): {
                    const auto& data = o.get<DrawOp::Rect>();
                    draw_rect(data.rect.a + origin, data.rect.b + origin, data.color);
                } break;
                case V::index_of<DrawOp::Scroll>(): {
                    const auto [w, h] = get_contents_size();
                    draw_rect(origin, origin + Point(w, h), 0x000000FF);
                    for(auto i = head, n = uint32_t(0); n == 0 || i != tail; i = (i + 1) % buffer.size(), n += 1) {
                        const auto& line = buffer[i];
                        for(auto c = 0; c < line.len; c += 1) {
                            draw_ascii(calc_position(n, c) + origin, line.data[c], 0xFFFFFFFF);
                        }
                    }
                } break;
                case V::index_of<DrawOp::Ascii>(): {
                    const auto& data = o.get<DrawOp::Ascii>();
                    draw_ascii(calc_position(data.row, data.column) + origin, data.ascii, data.color);
                } break;
                }
            }
            q = std::move(draw_queue.access().second);
        }
    }

    Terminal(const int width, const int height) : StandardWindow(width, height, "terminal"),
                                                  font_size(::get_font_size()) {
        resize(height / font_size[1], width / font_size[0]);
    }

    static auto main(const uint64_t id, const int64_t data) -> void {
        auto  app       = reinterpret_cast<Layer*>(data)->open_window<Terminal>(800, 600);
        auto& this_task = task::task_manager->get_current_task();
        auto  shell     = terminal::Shell([app](char c) { app->putc(c); }, [app](std::string_view s) { app->puts(s); });
        while(true) {
            const auto message = this_task.receive_message();
            if(!message) {
                this_task.sleep();
                continue;
            }
            switch(message->type) {
            case MessageType::Keyboard: {
                const auto& data = message->data.keyboard;
                shell.input(data.ascii);
            } break;
            default:
                break;
            }
        }
    }
};
