#include <array>
#include <cstdio>

#include "acpi.hpp"
#include "apps/counter.hpp"
#include "asmcode.h"
#include "console.hpp"
#include "interrupt.hpp"
#include "keyboard.hpp"
#include "log.hpp"
#include "memory-manager.hpp"
#include "memory-map.h"
#include "mouse.hpp"
#include "mousecursor.hpp"
#include "paging.hpp"
#include "pci.hpp"
#include "print.hpp"
#include "segment.hpp"
#include "task.hpp"
#include "timer.hpp"
#include "uefi/gop-framebuffer.hpp"
#include "usb/classdriver/hid.hpp"
#include "usb/xhci/xhci.hpp"
#include "virtio/gpu.hpp"
#include "window-manager.hpp"

auto kernel_queue = (std::deque<Message>*)(nullptr);

auto refresh() -> void {
    __asm__("cli");
    kernel_queue->push_back(MessageType::RefreshScreen);
    __asm__("sti");
}

auto task_b_entry(const uint64_t id, const int64_t data) -> void {
    auto& layer = *reinterpret_cast<Layer*>(data);
    auto  app   = layer.open_window<CounterApp>();
    while(true) {
        __asm__("hlt");
        app->increment();
        refresh();
    }
}

class Kernel {
  private:
    BitmapMemoryManager memory_manager;
    FramebufferConfig   framebuffer_config;
    acpi::RSDP&         rsdp;
    MouseCursor*        mousecursor;
    WindowManager*      window_manager;
    Window*             grubbed_window = nullptr;
    std::deque<Message> main_queue     = std::deque<Message>(32);

    // mouse click
    Point prev_mouse_pos = {0, 0};
    bool  left_pressed   = false;
    bool  right_pressed  = false;

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
        // setup memory manager
        setup_segments();
        set_dsall(kernel_ds);
        set_csss(kernel_cs, kernel_ss);
        setup_identity_page_table();
        memory_manager.initialize_heap();

        // set global objects
        allocator     = &memory_manager;
        const auto fb = std::unique_ptr<Framebuffer>(new uefi::Framebuffer(framebuffer_config));
        framebuffer   = fb.get();

        const auto wm                = std::unique_ptr<WindowManager>(new WindowManager());
        window_manager               = wm.get();
        const auto background_layer  = window_manager->create_layer();
        const auto application_layer = window_manager->create_layer();
        const auto mousecursor_layer = window_manager->create_layer();
        const auto fb_size           = framebuffer->get_size();
        ::console                    = window_manager->get_layer(background_layer).open_window<Console>(fb_size[0], fb_size[1]);

        // initialize acpi
        if(!acpi::initialize(rsdp)) {
            return;
        }

        // start timer
        timer::initialize_timer();
        auto timer_manager = timer::TimerManager(main_queue);

        // create task manager
        auto tm            = task::TaskManager();
        task::task_manager = &tm;

        // initialize idt
        interrupt::initialize(timer_manager, main_queue);

        // scan pci devices
        auto       pci              = pci::Devices();
        const auto error            = pci.scan_all_bus();
        const auto& [size, devices] = pci.get_devices();

        // find devices
        auto xhc_dev    = (const pci::Device*)(nullptr);
        auto virtio_gpu = (const pci::Device*)(nullptr);
        for(auto i = size_t(0); i < size; i += 1) {
            const auto& dev       = devices[i];
            const auto  vendor_id = dev.read_vender_id();
            logger(LogLevel::Debug, "%d.%d.%d: vend %04x, class %08x, head %02x\n", dev.bus, dev.device, dev.function, vendor_id, dev.class_code, dev.header_type);

            if(dev.class_code.match(0x0Cu, 0x03u, 0x30u)) {
                if(xhc_dev == nullptr || (xhc_dev->read_vender_id() != 0x8086 && vendor_id == 0x8086)) {
                    xhc_dev = &dev;
                }
            } else if(dev.read_vender_id() == 0x1AF4 && pci::read_device_id(dev.bus, dev.device, dev.function) == (0x1040 + 16)) {
                virtio_gpu = &dev;
            }
        }
        logger(LogLevel::Debug, "pci bus scan result: %s\n", error.to_str());
        const auto bsp_local_apic_id = *reinterpret_cast<const uint32_t*>(0xFEE00020) >> 24;
        if(xhc_dev->configure_msi_fixed_destination(bsp_local_apic_id, pci::MSITriggerMode::Level, pci::MSIDeliveryMode::Fixed, interrupt::InterruptVector::Number::XHCI, 0) &&
           xhc_dev->configure_msix_fixed_destination(bsp_local_apic_id, pci::MSITriggerMode::Level, pci::MSIDeliveryMode::Fixed, interrupt::InterruptVector::Number::XHCI, 0)) {
            logger(LogLevel::Error, "failed to configure msi for xHC device");
        }

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
        if(xhc_dev->read_vender_id() == 0x8086) {
            switch_ehci_to_xhci(pci, *xhc_dev);
        }
        logger(LogLevel::Debug, "xhc initialize: %s\n", xhc.initialize().to_str());
        xhc.run();

        // connect usb devices
        mouse::setup(main_queue);
        keyboard::setup(main_queue);

        for(auto i = 1; i <= xhc.get_max_ports(); i += 1) {
            auto port = xhc.get_port_at(i);
            if(port.is_connected()) {
                if(const auto error = xhc.configure_port(port)) {
                    logger(LogLevel::Error, "failed to configure port: %s\n", error.to_str());
                    continue;
                }
            }
        }

        auto gpu_device = std::optional<virtio::gpu::GPUDevice>();
        if(virtio_gpu != nullptr) {
            if(auto result = virtio::gpu::initialize(*virtio_gpu)) {
                gpu_device.emplace(std::move(result.as_value()));
            } else {
                logger(LogLevel::Error, "failed to initilize virtio gpu: %s", result.as_error().to_str());
            }
        }

        // create mouse cursor
        mousecursor = window_manager->get_layer(mousecursor_layer).open_window<MouseCursor>();

        // task switching timer
        timer_manager.add_timer({5, 0, timer::Flags::Periodic | timer::Flags::Task});

        kernel_queue = &main_queue;
        task::task_manager->new_task().init_context(task_b_entry, reinterpret_cast<int64_t>(&window_manager->get_layer(application_layer)));
        task::task_manager->new_task().init_context(task_b_entry, reinterpret_cast<int64_t>(&window_manager->get_layer(application_layer)));
        task::task_manager->new_task().init_context(task_b_entry, reinterpret_cast<int64_t>(&window_manager->get_layer(application_layer)));

    loop:
        __asm__("cli");
        if(main_queue.empty()) {
            __asm__("sti\n\thlt");
            goto loop;
        }
        const auto message = main_queue.front();
        main_queue.pop_front();
        __asm__("sti");

        switch(message.type) {
        case MessageType::XHCIInterrupt:
            while(xhc.has_unprocessed_event()) {
                if(const auto error = xhc.process_event()) {
                    logger(LogLevel::Error, "failed to process event: %s\n", error.to_str());
                }
            }
            break;
        case MessageType::Timer:
            break;
        case MessageType::Keyboard: {
            const auto& data = message.data.keyboard;
            printk("%c", data.ascii);
        } break;
        case MessageType::Mouse: {
            const auto& data = message.data.mouse;
            mousecursor->move_position(Point(data.displacement_x, data.displacement_y));
            if(grubbed_window != nullptr) {
                grubbed_window->move_position(mousecursor->get_position() - prev_mouse_pos);
                window_manager->get_layer(application_layer).focus(grubbed_window);
            }

            const auto left  = data.buttons & 0x01;
            const auto right = data.buttons & 0x02;

            prev_mouse_pos = mousecursor->get_position();
            if(!left_pressed & left) {
                grubbed_window = window_manager->try_grub(mousecursor->get_position());
            } else if(left_pressed & !left) {
                grubbed_window = nullptr;
            }
            left_pressed  = left;
            right_pressed = right;
            refresh();
        } break;
        case MessageType::RefreshScreen:
            window_manager->refresh();
            framebuffer->swap();
            break;
        case MessageType::VirtIOGPUControl:
            gpu_device->process_control_queue();
            break;
        case MessageType::VirtIOGPUCursor:
            break;
        }
        goto loop;
    }

    Kernel(const MemoryMap& memory_map, const FramebufferConfig& framebuffer_config, acpi::RSDP& rsdp) : memory_manager(memory_map),
                                                                                                         framebuffer_config(framebuffer_config),
                                                                                                         rsdp(rsdp) {}
};

alignas(16) uint8_t kernel_main_stack[1024 * 1024];

auto kernel_instance_buffer = std::array<uint8_t, sizeof(Kernel)>(); // max 4096 * 0x700 (Qemu)
static_assert(sizeof(Kernel) <= 4096 * 0x700, "the kernel size overs buffer size");

extern "C" void kernel_main(const MemoryMap& memory_map_ref, const FramebufferConfig& framebuffer_config_ref, acpi::RSDP& rsdp) {
    new(kernel_instance_buffer.data()) Kernel(memory_map_ref, framebuffer_config_ref, rsdp);
    reinterpret_cast<Kernel*>(kernel_instance_buffer.data())->run();
    while(1) {
        __asm__("hlt");
    }
}
