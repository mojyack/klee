#include <array>
#include <cstdio>

#include "console.hpp"
#include "mousecursor.hpp"
#include "pci.hpp"
#include "print.hpp"
#include "usb/classdriver/hid.hpp"
#include "usb/xhci/xhci.hpp"

auto console_buf = std::array<uint8_t, sizeof(Console)>();
auto console     = (Console*)(nullptr);

auto mousecursor_buf = std::array<uint8_t, sizeof(MouseCursor)>();
auto mousecursor     = (MouseCursor*)(nullptr);

auto mouse_observer(const int8_t displacement_x, const int8_t displacement_y) -> void {
    mousecursor->move_relative(Point(displacement_x, displacement_y));
    mousecursor->draw();
}

auto switch_ehci_to_xhci(const pci::Devices& pci, const pci::Device& xhc_dev) -> void {
    auto intel_ehc_exist       = false;
    const auto& [len, devices] = pci.get_devices();
    for(auto i = size_t(0); i < len; i += 1) {
        if(devices[i].class_code.match(0x0cu, 0x03u, 0x20u) /* EHCI */ && pci::read_vender_id(devices[i]) == 0x8086) {
            intel_ehc_exist = true;
            break;
        }
    }
    if(!intel_ehc_exist) {
        return;
    }

    const auto superspeed_ports = pci::read_register(xhc_dev, 0xDC); // USB3PRM
    pci::write_register(xhc_dev, 0xD8, superspeed_ports);            // USB3_PSSEN
    const auto ehci2xhci_ports = pci::read_register(xhc_dev, 0xD4);  // XUSB2PRM
    pci::write_register(xhc_dev, 0xD0, ehci2xhci_ports);             // XUSB2PR
}

extern "C" void kernel_main(const FramebufferConfig& framebuffer_config) {
    console     = new(console_buf.data()) Console(framebuffer_config);
    mousecursor = new(mousecursor_buf.data()) MouseCursor(framebuffer_config);

    printk("klee\n");

    auto       pci              = pci::Devices();
    const auto error            = pci.scan_all_bus();
    const auto& [size, devices] = pci.get_devices();
    auto xhc_dev                = (const pci::Device*)(nullptr);
    for(auto i = size_t(0); i < size; i += 1) {
        const auto& dev       = devices[i];
        const auto  vendor_id = pci::read_vender_id(dev.bus, dev.device, dev.function);
        printk("%d.%d.%d: vend %04x, class %08x, head %02x\n", dev.bus, dev.device, dev.function, vendor_id, dev.class_code, dev.header_type);

        if(dev.class_code.match(0x0Cu, 0x03u, 0x30u)) {
            if(xhc_dev == nullptr || (pci::read_vender_id(*xhc_dev) != 0x8086 && vendor_id == 0x8086)) {
                xhc_dev = &dev;
            }
        }
    }
    printk("pci bus scan result: %s\n", error.to_str());

    if(xhc_dev != nullptr) {
        const auto xhc_bar = pci::read_bar(*xhc_dev, 0);
        if(xhc_bar) {
            const auto xhc_mmio_base = xhc_bar.as_value() & ~static_cast<uint64_t>(0x0F);
            printk("xHC mmio_base = %08lx\n", xhc_mmio_base);
        }
    } else {
        printk("no xhc device found\n");
    }

    const auto xhc_bar = pci::read_bar(*xhc_dev, 0);
    if(!xhc_bar) {
        printk("failed to read xhc bar\n");
        while(1) {
            __asm__("hlt");
        }
    }
    const auto xhc_mmio_base = xhc_bar.as_value() & ~static_cast<uint64_t>(0x0F);
    auto       xhc           = usb::xhci::Controller(xhc_mmio_base);
    if(pci::read_vender_id(*xhc_dev) == 0x8086) {
        switch_ehci_to_xhci(pci, *xhc_dev);
    }
    printk("xhc initialize: %s\n", xhc.initialize().to_str());
    xhc.run();

    usb::HIDMouseDriver::default_observer = mouse_observer;

    for(auto i = 1; i <= xhc.get_max_ports(); i += 1) {
        auto port = xhc.get_port_at(i);
        if(port.is_connected()) {
            if(const auto error = xhc.configure_port(port)) {
                printk("failed to configure port: %s\n", error.to_str());
                continue;
            }
        }
    }

    while(1) {
        if(const auto error = xhc.process_event()) {
            printk("failed to process event: %s\n", error.to_str());
        }
    }

    while(1) {
        __asm__("hlt");
    }
}
