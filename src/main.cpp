#include "acpi.hpp"
#include "ahci/ahci.hpp"
#include "debug.hpp"
#include "devfs/framebuffer.hpp"
#include "fs/manager.hpp"
#include "interrupt/interrupt.hpp"
#include "keyboard.hpp"
#include "lapic/timer.hpp"
#include "memory-map.h"
#include "mouse.hpp"
#include "paging.hpp"
#include "panic.hpp"
#include "segment.hpp"
#include "smp/ap.hpp"
#include "smp/id.hpp"
#include "syscall.hpp"
#include "terminal.hpp"
#include "usb/classdriver/hid.hpp"
#include "usb/xhci/xhci.hpp"
#include "virtio/gpu.hpp"

class Kernel {
  private:
    smp::ProcessorResource processor_resource;
    segment::TSSResource   tss_resource;
    BitmapMemoryManager    memory_manager;
    FramebufferConfig      framebuffer_config;
    acpi::RSDP&            rsdp;

    std::vector<std::unique_ptr<smp::ProcessorResource>> processor_resources; // for aps

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

    static auto ap_main(smp::APBootParameter* const parameter) -> void {
        auto& processor_resource = *parameter->processor_resource;
        parameter->notify        = 1;
        apply_segments(processor_resource.gdt);
        apply_pml4_table(processor_resource.pml4_table);

        auto tss_resource = segment::TSSResource();
        if(auto r = segment::setup_tss(processor_resource.gdt); !r) {
            fatal_error("kernel: failed to setup tss: ", r.as_error().as_int());
        } else {
            tss_resource = std::move(r.as_value());
        }

        // enable lapic
        auto& lapic = lapic::get_registers();
        lapic.spurious_interrupt_vector |= 0b1'0000'0000; // software enable

        interrupt::initialize(processor_resource.idt);
        syscall::initialize_syscall();
        process::manager->capture_context(processor_resource.pml4_table);

        const auto this_thread = process::manager->get_this_thread();
        logger(LogLevel::Info, "kernel: processor %u ready, pid=%d tid=%d\n", smp::get_processor_number(), this_thread->process->id, this_thread->id);
        while(1) {
            process::manager->sleep_this_thread();
        }
    }

    auto boot_ap(const FrameID trampoline_page, const uint8_t lapic_id) -> Error {
        constexpr auto n_stack_frames = 1;

        auto resource = processor_resources.emplace_back(new(std::nothrow) smp::ProcessorResource).get();
        if(!resource) {
            return Error::Code::NoEnoughMemory;
        }
        if(auto r = allocator->allocate(n_stack_frames); !r) {
            return r.as_error();
        } else {
            resource->stack = std::move(r.as_value());
        }

        segment::create_segments(resource->gdt);
        paging::create_identity_page_table(resource->pml4_table);

        const auto boot_parameter = smp::APBootParameter{.processor_resource = resource, .notify = 0};
        const auto stack_ptr      = std::bit_cast<uint64_t>(resource->stack->get_frame()) + bytes_per_frame * n_stack_frames;
        smp::start_ap(trampoline_page, lapic_id, &ap_main, stack_ptr, &boot_parameter);
        processor_resources.emplace_back(std::move(resource));

        while(boot_parameter.notify == 0) {
            __asm__("pause");
        }
        return Success();
    }

    auto boot_aps(const FrameID trampoline_page) -> Error {
        const auto bsp_id = lapic::read_lapic_id();
        const auto ids    = acpi::detect_cores().lapic_ids;

        // create lapic_id to processor number table
        smp::first_lapic_id          = ids.front();
        smp::last_lapic_id           = ids.back();
        smp::lapic_id_to_index_table = new(std::nothrow) size_t[*std::max_element(ids.begin(), ids.end()) + 1];
        fatal_assert(smp::lapic_id_to_index_table != nullptr, "no enough memory");

        smp::lapic_id_to_index_table[bsp_id] = 0;
        for(auto i = 0, n = 1; i < ids.size(); i += 1) {
            if(ids[i] == bsp_id) {
                continue;
            }
            smp::lapic_id_to_index_table[ids[i]] = n;
            n += 1;

            logger(LogLevel::Info, "kernel: cpu core %d detected: lapic_id = %u\n", i, ids[i]);
        }

        // prepare process manager
        process::manager->expand_locals(ids.size());

        // boot aps
        for(const auto id : ids) {
            if(id == bsp_id) {
                continue;
            }
            if(const auto e = boot_ap(trampoline_page, id)) {
                logger(LogLevel::Error, "kernel: failed to boot ap: %d\n", e.as_int());
            }
        }
        return Success();
    }

  public:
    auto run() -> void {
        // setup memory manager
        segment::create_segments(processor_resource.gdt);
        segment::apply_segments(processor_resource.gdt);
        paging::create_identity_page_table(processor_resource.pml4_table);
        paging::apply_pml4_table(processor_resource.pml4_table);
        // - allocate lowest page for ap startup
        auto kernel_heap        = SmartFrameID();
        auto ap_trampoline_page = SmartFrameID();
        {
            auto r = memory_manager.allocate(1);                // this allocate must be performed before heap initialize
            if(auto r = memory_manager.initialize_heap(); !r) { // initialize heap here for printk
                fatal_error("failed to initialize heap memory");
            } else {
                kernel_heap = std::move(r.as_value());
            }
            if(!r) {
                printk("kernel: failed to allocate pages for ap startup; %d\n", r.as_error());
            } else {
                ap_trampoline_page = std::move(r.as_value());
            }
        }
        allocator = &memory_manager;

        // create debug output
        // TODO
        // remove this
        auto debug_fb = debug::Framebuffer(framebuffer_config);
        debug::fb     = &debug_fb;

        // create task manager
        // mutex needs this
        auto pm          = process::Manager(processor_resource.pml4_table);
        process::manager = &pm;

        // create filesystem mananger
        auto fs_manager = Critical<fs::Manager>();
        fs::critical_manager     = &fs_manager;

        // - mount "/dev"
        {
            auto& manager = fs::critical_manager->unsafe_access(); // no other threads exist here
            if(const auto e = manager.mount("devfs", "/dev")) {
                fatal_error("failed to mount \"/dev\": ", e.as_int());
            }
        }

        // create uefi framebuffer
        auto gop_framebuffer = devfs::GOPFrameBuffer(framebuffer_config);
        if(fs::critical_manager->unsafe_access().create_device_file("fb-uefi0", &gop_framebuffer)) {
            fatal_error("failed to create uefi framebuffer");
        }

        // initialize tss
        if(auto r = segment::setup_tss(processor_resource.gdt); !r) {
            fatal_error("failed to setup tss: ", r.as_error().as_int());
        } else {
            tss_resource = std::move(r.as_value());
        }

        // initialize acpi
        if(!acpi::initialize(rsdp)) {
            fatal_error("failde to initialize acpi");
            return;
        }

        // initialize idt
        interrupt::initialize(processor_resource.idt);

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
            if(const auto e = fs::critical_manager->unsafe_access().create_device_file("keyboard-usb0", usb_keyboard.get())) {
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

        const auto kernel_pid           = process::manager->get_this_thread()->process->id;
        auto       fs_device_finder_tid = process::ThreadID();
        if(sata_controller) {
            const auto tid_r = process::manager->create_thread(kernel_pid, fs::device_finder_main, reinterpret_cast<int64_t>(sata_controller.get()));
            fatal_assert(tid_r, "failed to create disk finder thread");
            const auto tid = tid_r.as_value();
            fatal_assert(!process::manager->wakeup_thread(kernel_pid, tid, -1), "failed to wakeup disk finder thread");
            fs_device_finder_tid = tid;
        }

        // boot aps
        if(acpi::madt != nullptr) {
            if(const auto e = boot_aps(*ap_trampoline_page)) {
                logger(LogLevel::Error, "kernel: ap boot failed: %d\n", e.as_int());
            }
        }
        ap_trampoline_page.free();

        // initialize syscall
        syscall::initialize_syscall();

        // start timer
        lapic::start_timer(interrupt::Vector::LAPICTimer);

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

        // thread test
        if(0){
            struct Main {
                static auto main(uint64_t id, int64_t data) -> void {
                loop:
                    printk("hello from processor %d\n", smp::get_processor_number());
                    process::manager->suspend_this_thread_for_ms(1000);
                    goto loop;
                }
            };
            const auto pid = process::manager->create_process();
            for(auto i = 0; i < 10; i += 1) {
                const auto tid_r = process::manager->create_thread(pid, Main::main, i);
                if(!tid_r) {
                    printk("failed to fork process\n");
                    break;
                }
                const auto tid = tid_r.as_value();
                fatal_assert(!process::manager->wakeup_thread(pid, tid, +2), "failed to wakeup thread");
            }
        }

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
                if(const auto e = fs::critical_manager->access().second.create_device_file("fb-virtio0", virtio_gpu_framebuffer.get())) {
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

alignas(Kernel) auto kernel_instance_buffer = std::array<std::byte, sizeof(Kernel)>(); // max 4096 * 0x700 (Qemu)
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

// syscall
namespace syscall {
extern "C" {
auto syscall_table = std::array<void*, 2>{
    (void*)syscall_printk,
    (void*)syscall_exit,
};

__attribute__((no_caller_saved_registers)) auto get_stack_ptr() -> uintptr_t {
    return process::manager->get_this_thread()->system_stack_address;
}
}
} // namespace syscall
