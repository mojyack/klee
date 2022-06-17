#include <array>
#include <cstdio>

#include "asmcode.h"
#include "console.hpp"
#include "interrupt.hpp"
#include "mousecursor.hpp"
#include "pci.hpp"
#include "print.hpp"
#include "queue.hpp"
#include "usb/classdriver/hid.hpp"
#include "usb/xhci/xhci.hpp"

enum class Message {
    XHCIInterrupt,
};

class Kernel {
  private:
    static inline Kernel* kernel;

    Console     console;
    MouseCursor mousecursor;

    usb::xhci::Controller* xhc;

    std::array<Message, 32> main_queue_data;
    ArrayQueue<Message>     main_queue = main_queue_data;

    auto mouse_observer(const int8_t displacement_x, const int8_t displacement_y) -> void {
        mousecursor.move_relative(Point(displacement_x, displacement_y));
        mousecursor.draw();
    }

    __attribute__((interrupt)) static auto int_hander_xhci(InterruptFrame* const frame) -> void {
        kernel->main_queue.push(Message::XHCIInterrupt);
        notify_end_of_interrupt();
    }

    static auto switch_ehci_to_xhci(const pci::Devices& pci, const pci::Device& xhc_dev) -> void {
        auto intel_ehc_exist       = false;
        const auto& [len, devices] = pci.get_devices();
        for(auto i = size_t(0); i < len; i += 1) {
            if(devices[i].class_code.match(0x0cu, 0x03u, 0x20u) /* EHCI */ && devices[i].read_vender_id() == 0x8086) {
                intel_ehc_exist = true;
                break;
            }
        }
        if(!intel_ehc_exist) {
            return;
        }

        const auto superspeed_ports = xhc_dev.read_register(0xDC); // USB3PRM
        xhc_dev.write_register(0xD8, superspeed_ports);            // USB3_PSSEN
        const auto ehci2xhci_ports = xhc_dev.read_register(0xD4);  // XUSB2PRM
        xhc_dev.write_register(0xD0, ehci2xhci_ports);             // XUSB2PR
    }

  public:
    auto run() -> void {
        kernel    = this;
        ::console = &console;

        printk("klee\n");

        const auto cs = read_cs();
        set_idt_entry(idt[InterruptVector::Number::XHCI], make_idt_attr(DescriptorType::InterruptGate, 0), reinterpret_cast<uint64_t>(int_hander_xhci), cs);
        load_idt(sizeof(idt) - 1, reinterpret_cast<uintptr_t>(&idt[0]));

        const auto bsp_local_apic_id = *reinterpret_cast<const uint32_t*>(0xFEE00020) >> 24;
        auto       pci               = pci::Devices();
        const auto error             = pci.scan_all_bus();
        const auto& [size, devices]  = pci.get_devices();
        auto xhc_dev                 = (const pci::Device*)(nullptr);
        for(auto i = size_t(0); i < size; i += 1) {
            const auto& dev       = devices[i];
            const auto  vendor_id = pci::read_vender_id(dev.bus, dev.device, dev.function);
            printk("%d.%d.%d: vend %04x, class %08x, head %02x\n", dev.bus, dev.device, dev.function, vendor_id, dev.class_code, dev.header_type);

            if(dev.class_code.match(0x0Cu, 0x03u, 0x30u)) {
                if(xhc_dev == nullptr || (xhc_dev->read_vender_id() != 0x8086 && vendor_id == 0x8086)) {
                    xhc_dev = &dev;
                }
            }
        }
        printk("pci bus scan result: %s\n", error.to_str());
        xhc_dev->configure_msi_fixed_destination(bsp_local_apic_id, pci::MSITriggerMode::Level, pci::MSIDeliveryMode::Fixed, InterruptVector::Number::XHCI, 0);

        const auto xhc_bar = xhc_dev->read_bar(0);
        if(xhc_dev != nullptr) {
            if(xhc_bar) {
                const auto xhc_mmio_base = xhc_bar.as_value() & ~static_cast<uint64_t>(0x0F);
                printk("xHC mmio_base = %08lx\n", xhc_mmio_base);
            }
        } else {
            printk("no xhc device found\n");
        }
        if(!xhc_bar) {
            printk("failed to read xhc bar\n");
            while(1) {
                __asm__("hlt");
            }
        }

        const auto xhc_mmio_base = xhc_bar.as_value() & ~static_cast<uint64_t>(0x0F);
        auto       xhc           = usb::xhci::Controller(xhc_mmio_base);
        this->xhc                = &xhc;
        if(xhc_dev->read_vender_id() == 0x8086) {
            switch_ehci_to_xhci(pci, *xhc_dev);
        }
        printk("xhc initialize: %s\n", xhc.initialize().to_str());
        xhc.run();

        usb::HIDMouseDriver::default_observer = [this](int8_t displacement_x, int8_t displacement_y) -> void {
            mouse_observer(displacement_x, displacement_y);
        };

        for(auto i = 1; i <= xhc.get_max_ports(); i += 1) {
            auto port = xhc.get_port_at(i);
            if(port.is_connected()) {
                if(const auto error = xhc.configure_port(port)) {
                    printk("failed to configure port: %s\n", error.to_str());
                    continue;
                }
            }
        }

    loop:
        __asm__("cli");
        if(main_queue.is_empty()) {
            __asm__("sti\n\thlt");
            goto loop;
        }
        const auto message = main_queue.get_front();
        main_queue.pop();
        __asm__("sti");

        switch(message) {
        case Message::XHCIInterrupt:
            while(kernel->xhc->has_unprocessed_event()) {
                if(const auto error = kernel->xhc->process_event()) {
                    // printk("failed to process event: %s\n", error.to_str());
                }
            }
            break;
        }
        goto loop;
    }

    Kernel(const FramebufferConfig& framebuffer_config) : console(framebuffer_config), mousecursor(framebuffer_config) {}
};

extern "C" void kernel_main(const FramebufferConfig& framebuffer_config) {
    auto kernel = Kernel(framebuffer_config);
    kernel.run();
    while(1) {
        __asm__("hlt");
    }
}
