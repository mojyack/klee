#include <array>
#include <cstdio>

#include "apps/counter.hpp"
#include "asmcode.h"
#include "console.hpp"
#include "interrupt.hpp"
#include "log.hpp"
#include "memory-manager.hpp"
#include "memory-map.h"
#include "mousecursor.hpp"
#include "paging.hpp"
#include "pci.hpp"
#include "print.hpp"
#include "queue.hpp"
#include "segment.hpp"
#include "timer.hpp"
#include "usb/classdriver/hid.hpp"
#include "usb/xhci/xhci.hpp"
#include "window-manager.hpp"

enum class Message {
    XHCIInterrupt,
};

class Kernel {
  private:
    static inline Kernel* kernel;

    MemoryMap           memory_map;
    BitmapMemoryManager memory_manager;
    FramebufferConfig   framebuffer_config;
    MouseCursor*        mousecursor;
    WindowManager*      window_manager;
    Window*             grubbed_window = nullptr;

    // mouse click
    Point prev_mouse_pos = {0, 0};
    bool  left_pressed   = false;
    bool  right_pressed  = false;

    usb::xhci::Controller* xhc;

    std::array<Message, 32> main_queue_data;
    ArrayQueue<Message>     main_queue = main_queue_data;

    auto mouse_observer(const uint8_t buttons, const int8_t displacement_x, const int8_t displacement_y) -> void {
        mousecursor->move_position(Point(displacement_x, displacement_y));
        if(grubbed_window != nullptr) {
            grubbed_window->move_position(mousecursor->get_position() - prev_mouse_pos);
        }

        const auto left  = buttons & 0x01;
        const auto right = buttons & 0x02;

        prev_mouse_pos = mousecursor->get_position();
        if(!left_pressed & left) {
            grubbed_window = window_manager->try_grub(mousecursor->get_position());
        } else if(left_pressed & !left) {
            grubbed_window = nullptr;
        }
        left_pressed  = left;
        right_pressed = right;
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
        // setup segments
        setup_segments();
        set_dsall(0);
        set_csss(1 << 3, 2 << 3);
        setup_identity_page_table();

        // initialize allocator
        const auto memory_map_base = reinterpret_cast<uintptr_t>(memory_map.buffer);
        auto       available_end   = uintptr_t(0);
        for(auto iter = memory_map_base; iter < memory_map_base + memory_map.map_size; iter += memory_map.descriptor_size) {
            const auto& desc = *reinterpret_cast<MemoryDescriptor*>(iter);
            if(available_end < desc.physical_start) {
                memory_manager.set_bits(FrameID(available_end / bytes_per_frame), (desc.physical_start - available_end) / bytes_per_frame, true);
            }

            const auto physical_end = desc.physical_start + desc.number_of_pages * uefi_page_size;
            if(is_available_memory_type(static_cast<MemoryType>(desc.type))) {
                available_end = physical_end;
            } else {
                memory_manager.set_bits(FrameID(desc.physical_start / bytes_per_frame), desc.number_of_pages * uefi_page_size / bytes_per_frame, true);
            }
        }
        memory_manager.set_range(FrameID(1), FrameID(available_end / bytes_per_frame));
        memory_manager.initialize_heap();

        // set global objects
        kernel        = this;
        allocator     = &memory_manager;
        const auto fb = std::unique_ptr<Framebuffer>(new Framebuffer(framebuffer_config));
        framebuffer   = fb.get();

        const auto wm                = std::unique_ptr<WindowManager>(new WindowManager());
        window_manager               = wm.get();
        const auto background_layer  = window_manager->create_layer();
        const auto appliction_layer  = window_manager->create_layer();
        const auto mousecursor_layer = window_manager->create_layer();
        const auto fb_size           = framebuffer->get_size();
        ::console                    = window_manager->get_layer(background_layer).open_window<Console>(fb_size[0], fb_size[1]);

        // create mouse cursor
        mousecursor = window_manager->get_layer(mousecursor_layer).open_window<MouseCursor>();

        // setup idt
        const auto cs = read_cs();
        set_idt_entry(idt[InterruptVector::Number::XHCI], make_idt_attr(DescriptorType::InterruptGate, 0), reinterpret_cast<uint64_t>(int_hander_xhci), cs);
        load_idt(sizeof(idt) - 1, reinterpret_cast<uintptr_t>(&idt[0]));

        // scan pci devices
        auto       pci              = pci::Devices();
        const auto error            = pci.scan_all_bus();
        const auto& [size, devices] = pci.get_devices();

        // find xhc device
        auto xhc_dev = (const pci::Device*)(nullptr);
        for(auto i = size_t(0); i < size; i += 1) {
            const auto& dev       = devices[i];
            const auto  vendor_id = pci::read_vender_id(dev.bus, dev.device, dev.function);
            logger(LogLevel::Debug, "%d.%d.%d: vend %04x, class %08x, head %02x\n", dev.bus, dev.device, dev.function, vendor_id, dev.class_code, dev.header_type);

            if(dev.class_code.match(0x0Cu, 0x03u, 0x30u)) {
                if(xhc_dev == nullptr || (xhc_dev->read_vender_id() != 0x8086 && vendor_id == 0x8086)) {
                    xhc_dev = &dev;
                }
            }
        }
        logger(LogLevel::Debug, "pci bus scan result: %s\n", error.to_str());
        const auto bsp_local_apic_id = *reinterpret_cast<const uint32_t*>(0xFEE00020) >> 24;
        xhc_dev->configure_msi_fixed_destination(bsp_local_apic_id, pci::MSITriggerMode::Level, pci::MSIDeliveryMode::Fixed, InterruptVector::Number::XHCI, 0);

        // find mmio address of xhc device
        const auto xhc_bar = xhc_dev->read_bar(0);
        if(xhc_dev != nullptr) {
            if(xhc_bar) {
                const auto xhc_mmio_base = xhc_bar.as_value() & ~static_cast<uint64_t>(0x0F);
                logger(LogLevel::Debug, "xHC mmio_base = %08lx\n", xhc_mmio_base);
            }
        } else {
            logger(LogLevel::Warn, "no xhc device found\n");
        }
        if(!xhc_bar) {
            logger(LogLevel::Error, "failed to read xhc bar\n");
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
        logger(LogLevel::Debug, "xhc initialize: %s\n", xhc.initialize().to_str());
        xhc.run();

        // connect usb devices
        usb::HIDMouseDriver::default_observer = [this](uint8_t buttons, int8_t displacement_x, int8_t displacement_y) -> void {
            mouse_observer(buttons, displacement_x, displacement_y);
        };

        for(auto i = 1; i <= xhc.get_max_ports(); i += 1) {
            auto port = xhc.get_port_at(i);
            if(port.is_connected()) {
                if(const auto error = xhc.configure_port(port)) {
                    logger(LogLevel::Error, "failed to configure port: %s\n", error.to_str());
                    continue;
                }
            }
        }

        // open counter app
        const auto counter_app = window_manager->get_layer(appliction_layer).open_window<CounterApp>();

    loop:
        counter_app->increment();
        window_manager->refresh();
        framebuffer->swap();
        __asm__("cli");
        if(main_queue.is_empty()) {
            __asm__("sti");
            goto loop;
        }
        const auto message = main_queue.get_front();
        main_queue.pop();
        __asm__("sti");

        switch(message) {
        case Message::XHCIInterrupt:
            while(kernel->xhc->has_unprocessed_event()) {
                if(const auto error = kernel->xhc->process_event()) {
                    logger(LogLevel::Error, "failed to process event: %s\n", error.to_str());
                }
            }
            break;
        }
        goto loop;
    }

    Kernel(const MemoryMap& memory_map, const FramebufferConfig& framebuffer_config) : memory_map(memory_map),
                                                                                       framebuffer_config(framebuffer_config) {}
};

alignas(16) uint8_t kernel_main_stack[1024 * 1024];

auto kernel_instance_buffer = std::array<uint8_t, sizeof(Kernel)>();

extern "C" void kernel_main(const MemoryMap& memory_map_ref, const FramebufferConfig& framebuffer_config_ref) {
    new(kernel_instance_buffer.data()) Kernel(memory_map_ref, framebuffer_config_ref);
    reinterpret_cast<Kernel*>(kernel_instance_buffer.data())->run();
    while(1) {
        __asm__("hlt");
    }
}
