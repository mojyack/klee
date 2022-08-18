#include <array>
#include <cstdio>

#include "acpi.hpp"
#include "ahci/ahci.hpp"
#include "apps/counter.hpp"
#include "apps/terminal.hpp"
#include "apps/wallpaper.hpp"
#include "asmcode.h"
#include "debug.hpp"
#include "devfs/framebuffer.hpp"
#include "fs/main.hpp"
#include "interrupt/interrupt.hpp"
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
#include "syscall.hpp"
#include "timer.hpp"
#include "uefi/gop-framebuffer.hpp"
#include "usb/classdriver/hid.hpp"
#include "usb/xhci/xhci.hpp"
#include "virtio/gpu.hpp"
#include "window-manager.hpp"
#include "terminal.hpp"

class Kernel {
  private:
    BitmapMemoryManager memory_manager;
    FramebufferConfig   framebuffer_config;
    acpi::RSDP&         rsdp;
    MouseCursor*        mousecursor;
    WindowManager*      window_manager;

    Window* focused_window         = nullptr;
    bool    focused_window_grubbed = false;

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
        segment::setup_segments();
        paging::setup_identity_page_table();
        memory_manager.initialize_heap();
        allocator = &memory_manager;

        // create task manager
        // mutex needs this
        auto tm            = task::TaskManager();
        task::manager = &tm;

        // create debug output
        // TODO
        // remove this
        auto debug_fb = debug::Framebuffer(framebuffer_config);
        debug::fb     = &debug_fb;

        // create filesystem mananger
        auto fs_manager = Critical<fs::FilesystemManager>();
        fs::manager     = &fs_manager;
        // mount "/dev"
        {
            auto& manager = fs::manager->unsafe_access(); // no other threads exist here
            if(const auto e = manager.mount("devfs", "/dev")) {
                debug::println("failed to mount \"/dev\": ", e.as_int());
                return;
            }
        }

        // create uefi framebuffer
        auto gop_framebuffer = devfs::GOPFrameBuffer(framebuffer_config);
        if(devfs::create_device_file("fb-uefi0", reinterpret_cast<uintptr_t>(&gop_framebuffer))) {
            return;
        }

        // set global objects

        const auto fb = std::unique_ptr<Framebuffer>(new uefi::Framebuffer(framebuffer_config));
        framebuffer   = fb.get();

        const auto wm                = std::unique_ptr<WindowManager>(new WindowManager());
        window_manager               = wm.get();
        const auto background_layer  = window_manager->create_layer();
        const auto application_layer = window_manager->create_layer();
        const auto mousecursor_layer = window_manager->create_layer();
        const auto fb_size           = framebuffer->get_size();

        // initialize tss
        if(const auto e = segment::setup_tss()) {
            logger(LogLevel::Error, "failed to setup tss\n");
        }

        // initialize syscall
        syscall::initialize_syscall();

        // initialize acpi
        if(!acpi::initialize(rsdp)) {
            return;
        }

        // start timer
        timer::initialize_timer();
        auto timer_manager = timer::TimerManager();

        // initialize idt
        interrupt::initialize(timer_manager);

        // create background
        const auto background = window_manager->get_layer(background_layer).open_window<WallpaperApp>(fb_size[0], fb_size[1]);

        // create mouse cursor
        mousecursor = window_manager->get_layer(mousecursor_layer).open_window<MouseCursor>();

        // task switching timer
        timer_manager.add_timer({5, 0, timer::Flags::Periodic | timer::Flags::Task});

        task::kernel_task = &task::manager->get_current_task();

        // scan pci devices
        auto       pci              = pci::Devices();
        const auto error            = pci.scan_all_bus();
        const auto& [size, devices] = pci.get_devices();

        // find devices
        auto xhc_dev    = (const pci::Device*)(nullptr);
        auto virtio_gpu = (const pci::Device*)(nullptr);
        auto ahci_dev   = (const pci::Device*)(nullptr);
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
            } else if(dev.class_code.match(0x01, 0x06)) {
                ahci_dev = &dev;
            }
        }
        logger(LogLevel::Debug, "pci bus scan result: %d\n", error);
        const auto bsp_local_apic_id = *reinterpret_cast<const uint32_t*>(0xFEE00020) >> 24;
        if(xhc_dev->configure_msi_fixed_destination(bsp_local_apic_id, pci::MSITriggerMode::Level, pci::MSIDeliveryMode::Fixed, interrupt::Vector::XHCI, 0) &&
           xhc_dev->configure_msix_fixed_destination(bsp_local_apic_id, pci::MSITriggerMode::Level, pci::MSIDeliveryMode::Fixed, interrupt::Vector::XHCI, 0)) {
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
        logger(LogLevel::Debug, "xhc initialize: %d\n", xhc.initialize());
        xhc.run();

        // connect usb devices
        auto usb_keyboard = devfs::USBKeyboard();
        if(devfs::create_device_file("keyboard-usb0", reinterpret_cast<uintptr_t>(&usb_keyboard))) {
            return;
        }
        mouse::setup();
        keyboard::setup(usb_keyboard);

        for(auto i = 1; i <= xhc.get_max_ports(); i += 1) {
            auto port = xhc.get_port_at(i);
            if(port.is_connected()) {
                if(const auto error = xhc.configure_port(port)) {
                    logger(LogLevel::Error, "failed to configure port: %d\n", error);
                    continue;
                }
            }
        }

        auto gpu_device = std::optional<virtio::gpu::GPUDevice>();
        if(virtio_gpu != nullptr) {
            if(auto result = virtio::gpu::initialize(*virtio_gpu)) {
                gpu_device.emplace(std::move(result.as_value()));
            } else {
                logger(LogLevel::Error, "failed to initilize virtio gpu: %d", result.as_error());
            }
        }

        auto sata_controller = std::optional<ahci::Controller>();
        if(ahci_dev != nullptr) {
            sata_controller = ahci::initialize(*ahci_dev);
        }

        auto& disk_finder = task::manager->new_task();
        disk_finder.init_context(fs::device_finder_main, reinterpret_cast<int64_t>(&sata_controller.value()));
        disk_finder.wakeup(1);

        // open terminal
        //{
        auto& guiterm = task::manager->new_task();
        guiterm.init_context(Terminal::main, reinterpret_cast<int64_t>(&window_manager->get_layer(application_layer)));
        guiterm.wakeup();
        //}
        
        auto& term = task::manager->new_task();
        term.init_context(terminal::main, 0);
        term.wakeup();

        refresh();

        auto refresh_screen_done = true;
        auto refresh_pending     = false;

        printk("klee.\n");

    loop:
        __asm__("cli");
        const auto message = task::kernel_task->receive_message();
        __asm__("sti");
        if(!message) {
            task::kernel_task->sleep();
            goto loop;
        }

        //debug::println("event %d\n", (int)message->type);

        switch(message->type) {
        case MessageType::XHCIInterrupt:
            while(xhc.has_unprocessed_event()) {
                if(const auto error = xhc.process_event()) {
                    logger(LogLevel::Error, "failed to process event: %d\n", error);
                }
            }
            break;
        case MessageType::AHCIInterrupt:
            sata_controller->on_interrupt();
            break;
        case MessageType::Timer:
            break;
        case MessageType::Keyboard: {
            if(focused_window != nullptr) {
                focused_window->get_task().send_message(*message);
            }
        } break;
        case MessageType::Mouse: {
            const auto& data = message->data.mouse;
            mousecursor->move_position(Point(data.displacement_x, data.displacement_y));
            if(focused_window != nullptr && focused_window_grubbed) {
                focused_window->move_position(mousecursor->get_position() - prev_mouse_pos);
            }

            const auto left  = data.buttons & 0x01;
            const auto right = data.buttons & 0x02;

            prev_mouse_pos = mousecursor->get_position();
            if(!left_pressed & left) {
                auto&      a           = window_manager->get_layer(application_layer);
                const auto f           = a.try_focus(mousecursor->get_position());
                focused_window         = f != nullptr ? f : focused_window;
                const auto g           = a.try_grub(mousecursor->get_position());
                focused_window_grubbed = focused_window == g;
                if(focused_window != nullptr) {
                    a.focus(focused_window);
                }
            } else if(left_pressed & !left) {
                focused_window_grubbed = false;
            }
            left_pressed  = left;
            right_pressed = right;
            refresh();
        } break;
        case MessageType::RefreshScreen:
            if(!refresh_screen_done) {
                refresh_pending = true;
                break;
            }
            refresh_screen_done = false;
            refresh_pending     = false;
            window_manager->refresh(focused_window);
            framebuffer->swap();
            break;
        case MessageType::RefreshScreenDone:
            refresh_screen_done = true;
            if(refresh_pending) {
                refresh();
            }
            break;
        case MessageType::ScreenResized: {
            const auto [w, h] = framebuffer->get_size();
            background->resize_window(w, h);
        } break;
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

// functions below is referenced by asmcode
// therefore, it can't be defined as an inline function

// interrupt/interrupt.hpp
namespace interrupt::internal {
extern "C" auto int_handler_lapic_timer(task::TaskContext& context) -> void {
    const auto task_switch = timer_manager->count_tick();
    notify_end_of_interrupt();

    if(task_switch) {
        task::manager->switch_task_may_fail(context);
    }
}
} // namespace interrupt::internal

// syscall.hpp
namespace syscall {
extern "C" auto syscall_table = std::array<SyscallFunc*, 2>{
    syscall_printk,
    syscall_exit,
};
}

// task/manager.hpp
extern "C" auto self_task_system_stack = uint64_t();
