#pragma once
#include "fs/main.hpp"

namespace terminal {
inline auto main(const uint64_t id, const int64_t data) -> void {
    auto framebuffer_path = "/dev/fb-uefi0";
    auto keyboard_path = "/dev/keyboard-usb0";
    
    auto keyboard_result = Result<fs::Handle>();
    {
        auto [lock, manager] = fs::manager->access();
        auto& root = manager.get_fs_root();
        keyboard_result = root.open(keyboard_path, fs::OpenMode::Read);
    }
    if(!keyboard_result) {
        task::task_manager->get_current_task().exit();
    }

    auto& keyboard_handle = keyboard_result.as_value();

    auto buf = fs::dev::KeyboardPacket();
    
    while(keyboard_handle.read(0, sizeof(fs::dev::KeyboardPacket), &buf)) {
        printk("%c %02X %02X\n", buf.ascii, buf.keycode, buf.modifier);
    }
    printk("terminal exit\n");

    task::task_manager->get_current_task().exit();
}
}
