#include "acpi.hpp"
#include "ahci/ahci.hpp"
#include "debug.hpp"
#include "devfs/framebuffer.hpp"
#include "fs/manager-impl.hpp"
#include "interrupt/interrupt.hpp"
#include "keyboard.hpp"
#include "memory-map.h"
#include "mouse.hpp"
#include "paging.hpp"
#include "panic.hpp"
#include "segment.hpp"
#include "syscall.hpp"
#include "terminal.hpp"
#include "timer.hpp"
#include "usb/classdriver/hid.hpp"
#include "usb/xhci/xhci.hpp"
#include "virtio/gpu.hpp"

class Kernel {
  private:
    BitmapMemoryManager memory_manager;
    FramebufferConfig   framebuffer_config;
    acpi::RSDP&         rsdp;

    static auto switch_ehci_to_xhci(const std::vector<pci::Device>& devices, const pci::Device& xhc_dev) -> void {
        auto intel_ehc_exist = false;
        for(const auto& dev : devices) {
            if(dev.class_code.match(0x0cu, 0x03u, 0x20u) /* EHCI */ && dev.read_vender_id() == 0x8086) {
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

    struct PCIScanResult {
        std::vector<pci::Device> devices;
        const pci::Device*       xhc;
        const pci::Device*       virtio_gpu;
        const pci::Device*       ahci;
    };

    static auto scan_pci_devices() -> PCIScanResult {
        logger(LogLevel::Info, "kernel: scanning pci devices...\n");
        auto devices = pci::scan_devices();

        auto xhc        = (const pci::Device*)(nullptr);
        auto virtio_gpu = (const pci::Device*)(nullptr);
        auto ahci       = (const pci::Device*)(nullptr);

        logger(LogLevel::Info, "kernel: %lu pci devices found. searching pci devices...\n", devices.size());
        for(auto& dev : devices) {
            const auto vendor_id = dev.read_vender_id();
            logger(LogLevel::Info, "  %d.%d.%d: vend %04x, class %08x, head %02x\n", dev.bus, dev.device, dev.function, vendor_id, dev.class_code, dev.header_type);

            if(dev.class_code.match(0x0Cu, 0x03u, 0x30u)) {
                if(xhc == nullptr || (xhc->read_vender_id() != 0x8086 && vendor_id == 0x8086)) {
                    xhc = &dev;
                }
            } else if(dev.read_vender_id() == 0x1AF4 && pci::read_device_id(dev.bus, dev.device, dev.function) == (0x1040 + 16)) {
                virtio_gpu = &dev;
            } else if(dev.class_code.match(0x01, 0x06)) {
                ahci = &dev;
            }
        }
        return PCIScanResult{.devices = std::move(devices), .xhc = xhc, .virtio_gpu = virtio_gpu, .ahci = ahci};
    }

    static auto setup_xhc(const pci::Device& dev) -> std::optional<std::unique_ptr<usb::xhci::Controller>> {
        const auto bsp_local_apic_id = *reinterpret_cast<const uint32_t*>(0xFEE00020) >> 24;

        if(dev.configure_msi_fixed_destination(bsp_local_apic_id, pci::MSITriggerMode::Level, pci::MSIDeliveryMode::Fixed, interrupt::Vector::XHCI, 0) &&
           dev.configure_msix_fixed_destination(bsp_local_apic_id, pci::MSITriggerMode::Level, pci::MSIDeliveryMode::Fixed, interrupt::Vector::XHCI, 0)) {
            logger(LogLevel::Error, "kernel: failed to configure msi for xHC device");
            return std::nullopt;
        }

        // find mmio address of xhc device
        const auto xhc_bar_r = dev.read_bar(0);
        if(!xhc_bar_r) {
            logger(LogLevel::Error, "kernel: failed to read xhc bar\n");
            return std::nullopt;
        }
        const auto xhc_bar = xhc_bar_r.as_value();

        const auto xhc_mmio_base = xhc_bar & ~static_cast<uint64_t>(0x0F);
        logger(LogLevel::Debug, "kernel: xHC mmio_base=%08lx\n", xhc_mmio_base);

        auto xhc = std::unique_ptr<usb::xhci::Controller>(new usb::xhci::Controller(xhc_mmio_base));
        if(const auto e = xhc->initialize()) {
            logger(LogLevel::Error, "kernel: failed to initialize xhc: %d", e.as_int());
            return std::nullopt;
        }
        if(const auto e = xhc->run()) {
            logger(LogLevel::Error, "kernel: failed to start xhc: %d", e.as_int());
            return std::nullopt;
        }

        for(auto i = 1; i <= xhc->get_max_ports(); i += 1) {
            auto port = xhc->get_port_at(i);
            if(!port.is_connected()) {
                continue;
            }
            if(const auto e = xhc->configure_port(port)) {
                logger(LogLevel::Error, "kernel: failed to configure port: %d\n", e);
                return std::nullopt;
            }
        }

        return std::move(xhc);
    }

  public:
    auto run() -> void {
        // setup memory manager
        segment::setup_segments();
        paging::setup_identity_page_table();
        fatal_assert(!memory_manager.initialize_heap(), "failed to initialize heap memory");
        allocator = &memory_manager;

        // create debug output
        // TODO
        // remove this
        auto debug_fb = debug::Framebuffer(framebuffer_config);
        debug::fb     = &debug_fb;

        // create task manager
        // mutex needs this
        auto pm          = process::Manager();
        process::manager = &pm;

        // create filesystem mananger
        auto fs_manager = Critical<fs::FilesystemManager>();
        fs::manager     = &fs_manager;

        // - mount "/dev"
        {
            auto& manager = fs::manager->unsafe_access(); // no other threads exist here
            if(const auto e = manager.mount("devfs", "/dev")) {
                debug::println("failed to mount \"/dev\": ", e.as_int());
                return;
            }
        }

        // create uefi framebuffer
        auto gop_framebuffer = devfs::GOPFrameBuffer(framebuffer_config);
        if(fs::manager->unsafe_access().create_device_file("fb-uefi0", &gop_framebuffer)) {
            debug::println("failed to create uefi framebuffer");
            return;
        }

        // initialize tss
        if(const auto e = segment::setup_tss()) {
            fatal_error("failed to setup tss: ", e.as_int());
        }

        // initialize syscall
        syscall::initialize_syscall();

        // initialize acpi
        if(!acpi::initialize(rsdp)) {
            fatal_error("failde to initialize acpi");
            return;
        }
        if(acpi::madt != nullptr) {
            const auto cores = acpi::detect_cores();
            for(auto i = 0; i < cores.lapic_ids.size(); i += 1) {
                logger(LogLevel::Info, "kernel: cpu core %d detected: lapic_id = %u\n", i, cores.lapic_ids[i]);
            }
        }

        // start timer
        timer::initialize_timer();

        // initialize idt
        interrupt::initialize();

        // setup devices
        auto usb_keyboard = std::unique_ptr<devfs::USBKeyboard>();

        // - setup pci devices
        const auto pci_devices = scan_pci_devices();

        // -- setup xhc
        auto xhc = std::unique_ptr<usb::xhci::Controller>();
        if(pci_devices.xhc != nullptr) {
            if(pci_devices.xhc->read_vender_id() == 0x8086) {
                switch_ehci_to_xhci(pci_devices.devices, *pci_devices.xhc);
            }
            if(auto x = setup_xhc(*pci_devices.xhc)) {
                xhc = std::move(*x);
                usb_keyboard.reset(new devfs::USBKeyboard());
            }
        } else {
            logger(LogLevel::Warn, "kernel: no xhc device found\n");
        }

        // --- connect usb devices
        if(usb_keyboard) {
            if(const auto e = fs::manager->unsafe_access().create_device_file("keyboard-usb0", usb_keyboard.get())) {
                logger(LogLevel::Error, "kernel: failed to create keyboard device file: %d\n", e.as_int());
            } else {
                keyboard::setup(*usb_keyboard.get());
            }
        }
        // TODO
        // implement usb mouse device file
        // mouse::setup();

        // -- setup virtio gpu
        auto virtio_gpu = std::unique_ptr<virtio::gpu::GPUDevice>();
        if(pci_devices.virtio_gpu != nullptr) {
            if(auto result = virtio::gpu::initialize(*pci_devices.virtio_gpu)) {
                virtio_gpu.reset(new virtio::gpu::GPUDevice(std::move(result.as_value())));
            } else {
                logger(LogLevel::Error, "kernel: failed to initilize virtio gpu: %d", result.as_error());
            }
        }

        auto virtio_gpu_framebuffer = std::unique_ptr<virtio::gpu::Framebuffer>(); // used later

        // -- setup sata devices
        auto sata_controller = std::unique_ptr<ahci::Controller>();
        if(pci_devices.ahci != nullptr) {
            sata_controller = ahci::initialize(*pci_devices.ahci);
        }

        const auto kernel_pid           = process::manager->get_this_thread()->id;
        auto       fs_device_finder_tid = process::ThreadID();
        if(sata_controller) {
            const auto tid_r = process::manager->create_thread(kernel_pid, fs::device_finder_main, reinterpret_cast<int64_t>(sata_controller.get()));
            fatal_assert(tid_r, "failed to create disk finder thread");
            const auto tid = tid_r.as_value();
            fatal_assert(!process::manager->wakeup_thread(kernel_pid, tid, -1), "failed to wakeup disk finder thread");
            fs_device_finder_tid = tid;
        }

        // create terminal
        auto terminal_fb_dev = "/dev/fb-uefi0";
        {
            const auto pid   = process::manager->create_process();
            const auto tid_r = process::manager->create_thread(pid, terminal::main, reinterpret_cast<int64_t>(&terminal_fb_dev));
            fatal_assert(tid_r, "failed to create terminal thread");
            const auto tid = tid_r.as_value();
            fatal_assert(!process::manager->wakeup_thread(pid, tid), "failed to wakeup terminal thread");
        }

        logger(LogLevel::Info, "kernel: initialize done\n");

    loop:
        const auto& messages = kernel_message_queue.swap();
        if(messages.empty()) {
            process::manager->sleep_this_thread();
            goto loop;
        }

        for(const auto& m : messages) {
            switch(m.type) {
            case MessageType::XHCIInterrupt:
                while(xhc->has_unprocessed_event()) {
                    if(const auto error = xhc->process_event()) {
                        logger(LogLevel::Error, "kernel: failed to process xhc event: %d\n", error);
                    }
                }
                break;
            case MessageType::AHCIInterrupt:
                sata_controller->on_interrupt();
                break;
            case MessageType::Timer:
                break;
            case MessageType::VirtIOGPUNewDevice: {
                virtio_gpu_framebuffer = virtio_gpu->create_devfs_framebuffer();
                if(const auto e = fs::manager->access().second.create_device_file("fb-virtio0", virtio_gpu_framebuffer.get())) {
                    logger(LogLevel::Error, "kernel: failed to create virtio gpu device file: %d\n", e.as_int());
                }
                terminal_fb_dev = "/dev/fb-virtio0";
            } break;
            case MessageType::VirtIOGPUControl:
                if(const auto e = virtio_gpu->process_control_queue()) {
                    logger(LogLevel::Error, "kernel: failed to process virtio gpu event: %d\n", e.as_int());
                }
                break;
            case MessageType::VirtIOGPUCursor:
                break;
            case MessageType::DeviceFinderDone:
                if(const auto e = process::manager->wait_thread(kernel_pid, fs_device_finder_tid)) {
                    logger(LogLevel::Error, "kernel: failed to finish device finder thread\n");
                }
                break;
            }
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
extern "C" auto int_handler_lapic_timer(process::ThreadContext& context) -> void {
    notify_end_of_interrupt();
    process::manager->switch_thread_may_fail(context);
}
} // namespace interrupt::internal

// syscall.hpp
namespace syscall {
extern "C" auto syscall_table = std::array<void*, 2>{
    (void*)syscall_printk,
    (void*)syscall_exit,
};
}
