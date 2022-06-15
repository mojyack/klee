#include <array>
#include <cstdio>

#include "console.hpp"
#include "mousecursor.hpp"
#include "pci.hpp"
#include "print.hpp"

auto console_buf = std::array<uint8_t, sizeof(Console)>();
auto console     = (Console*)(nullptr);

extern "C" void kernel_main(const FramebufferConfig& framebuffer_config) {
    console = new(console_buf.data()) Console(framebuffer_config);
    printk("klee\n");

    auto mousecursor = Point{framebuffer_config.horizontal_resolution / 2, framebuffer_config.vertical_resolution / 2};
    draw_mousecursor(framebuffer_config, mousecursor);

    auto       pci              = pci::Devices();
    const auto error            = pci.scan_all_bus();
    const auto& [size, devices] = pci.get_devices();
    for(auto i = size_t(0); i < size; i += 1) {
        const auto& dev        = devices[i];
        auto        vendor_id  = pci::read_vender_id(dev.bus, dev.device, dev.function);
        auto        class_code = pci::read_class_code(dev.bus, dev.device, dev.function);
        printk("%d.%d.%d: vend %04x, class %08x, head %02x\n", dev.bus, dev.device, dev.function, vendor_id, class_code, dev.header_type);
    }
    printk("pci bus scan result: %s\n", error.to_str());

    while(1) {
        __asm__("hlt");
    }
}
