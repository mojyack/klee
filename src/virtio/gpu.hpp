#pragma once
#include <algorithm>

#include "../fs/drivers/dev/fb.hpp"
#include "../interrupt/vector.hpp"
#include "../lapic/registers.hpp"
#include "../log.hpp"
#include "../pci.hpp"
#include "../util/coroutine.hpp"
#include "pci.hpp"
#include "queue.hpp"

namespace virtio::gpu {
namespace internal {
struct DeviceConfig {
    using Event = uint32_t;

    static constexpr auto event_display = Event(1 << 0);

    Event    events_read;
    uint32_t events_clear;
    uint32_t num_scanouts;
    uint32_t reserved;
} __attribute__((packed));

enum class Control : uint32_t {
    // 2d commands
    GetDisplayInfo = 0x0100,
    ResourceCreate2D,
    ResourceUnref,
    SetScanout,
    ResourceFlush,
    TransferToHost2D,
    ResourceAttachBacking,
    ResourceDetachBacking,
    GetCapsetInfo,
    GetCapset,
    GetEDID,

    // cursor commands
    UpdateCursor = 0x0300,
    MoveCursor,

    // success response
    OkNodata = 0x1100,
    OkDisplayInfo,
    OkCapsetInfo,
    OkCapset,
    OkEDID,

    // error response
    ErrorUnspec = 0x1200,
    ErrorOutOfMemory,
    ErrorInvalidScanoutID,
    ErrorInvalidResourceID,
    ErrorInvalidContextID,
    ErrorInvalidParameter,
};

struct ControlHeader {
    using Flags = uint32_t;

    static constexpr auto flag_fence = Flags(1 << 0);

    Control  type;
    Flags    flags;
    uint64_t fence_id;
    uint32_t context_id;
    uint32_t padding;
} __attribute__((packed));

struct Rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct GetDisplayInfoResponse {
    static constexpr auto info_size = 16;

    struct {
        Rect     rect;
        uint32_t enabled;
        uint32_t flags;
    } modes[info_size];
} __attribute__((packed));

struct GetEDITRequest {
    // TODO
} __attribute__((packed));

struct GetEDITResponse {
    // TODO
} __attribute__((packed));

enum class Formats : uint32_t {
    B8G8R8A8Unorm = 1,
    B8G8R8X8Unorm = 2,
    A8R8G8B8Unorm = 3,
    X8R8G8B8Unorm = 4,

    R8G8B8A8Unorm = 67,
    X8B8G8R8Unorm = 68,

    A8B8G8R8Unorm = 121,
    R8G8B8X8Unorm = 134,
};

struct ResourceCreate2DRequest {
    uint32_t resource_id;
    Formats  format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct ResourceUnrefRequest {
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct SetScanoutRequest {
    Rect     rect;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

struct ResourceFlushRequest {
    Rect     rect;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct TransferToHost2DRequest {
    Rect     rect;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct ResourceAttachBackingRequest {
    struct MemEntry {
        uint64_t address;
        uint32_t length;
        uint32_t padding;
    } __attribute__((packed));

    uint32_t resource_id;
    uint32_t num_entries;
    MemEntry entries[];
} __attribute__((packed));

struct ResourceDetachBackingRequest {
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

constexpr auto buffer_size_max = sizeof(ControlHeader) + std::max({sizeof(GetDisplayInfoResponse),
                                                                   sizeof(GetEDITRequest), sizeof(GetEDITResponse),
                                                                   sizeof(ResourceCreate2DRequest),
                                                                   sizeof(ResourceUnrefRequest),
                                                                   sizeof(SetScanoutRequest),
                                                                   sizeof(ResourceFlushRequest),
                                                                   sizeof(TransferToHost2DRequest),
                                                                   sizeof(ResourceAttachBackingRequest),
                                                                   sizeof(ResourceDetachBackingRequest)});

inline auto queue_data(queue::Queue& queue, ControlHeader&& header) -> void {
    const auto b = static_cast<uint8_t*>(queue.get_next_descriptor_buffer(sizeof(ControlHeader)));
    const auto h = reinterpret_cast<ControlHeader*>(b + 0);
    *h           = std::move(header);
}

template <class Payload>
auto queue_data(queue::Queue& queue, ControlHeader&& header, Payload&& payload) -> void {
    const auto b = static_cast<uint8_t*>(queue.get_next_descriptor_buffer(sizeof(ControlHeader) + sizeof(Payload)));
    const auto h = reinterpret_cast<ControlHeader*>(b + 0);
    const auto p = reinterpret_cast<Payload*>(b + sizeof(ControlHeader));
    *h           = std::move(header);
    *p           = std::move(payload);
}
} // namespace internal

class Framebuffer : public fs::dev::FramebufferDevice {
  private:
    std::array<memory::FrameID, 2> buffers;
    queue::Queue*                  control_queue;
    uint32_t*                      sync_done;
    bool                           flip = false;

  public:
    auto swap() -> void override {
        if(*sync_done == 0) {
            return;
        }
        const auto resource_id = uint32_t(!flip ? 1 : 2);

        using namespace internal;
        queue_data<TransferToHost2DRequest>(*control_queue, {Control::TransferToHost2D, 0, 0, 0}, {{0, 0, uint32_t(buffer_size[0]), uint32_t(buffer_size[1])}, 0, resource_id, 0});
        queue_data<ResourceFlushRequest>(*control_queue, {Control::ResourceFlush, 0, 0, 0}, {{0, 0, uint32_t(buffer_size[0]), uint32_t(buffer_size[1])}, resource_id, 0});
        queue_data<SetScanoutRequest>(*control_queue, {Control::SetScanout, ControlHeader::flag_fence, 1, 0}, {{0, 0, uint32_t(buffer_size[0]), uint32_t(buffer_size[1])}, 0, resource_id});
        control_queue->notify_device();

        *sync_done = 0;
        flip       = !flip;
        data       = static_cast<std::byte*>(buffers[flip].get_frame());
        return;
    }

    auto is_double_buffered() const -> bool override {
        return true;
    }

    auto operator=(Framebuffer&&) -> Framebuffer& = delete;

    Framebuffer(const std::array<memory::FrameID, 2> buffers, const std::array<size_t, 2> buffer_size, queue::Queue& control_queue, uint32_t* const sync_done, Event*& sync_done_event) : buffers(buffers),
                                                                                                                                                                                          control_queue(&control_queue),
                                                                                                                                                                                          sync_done(sync_done) {
        data              = static_cast<std::byte*>(buffers[flip].get_frame());
        this->buffer_size = buffer_size;
        sync_done_event   = &write_event;
    };
};

class GPUDevice {
  private:
    struct ResumeArg {
        internal::ControlHeader* request;
        internal::ControlHeader* response;
    };

    using Worker = CoRoutine<ResumeArg, Error>;

    queue::Queue control_queue;
    queue::Queue cursor_queue;

    std::array<uint32_t, 2>                                  display_size = {1024, 768};
    std::pair<std::array<memory::SmartFrameID, 2>, uint64_t> framebuffer;
    Worker                                                   worker;
    uint32_t                                                 sync_done       = 1;
    Event*                                                   sync_done_event = nullptr;

  public:
    auto worker_main() -> Worker::Generator {
        using namespace internal;

        auto& request  = worker.resume_arg.request;
        auto& response = worker.resume_arg.response;

        // display info
        {
            if(request->type != Control::GetDisplayInfo) {
                logger(LogLevel::Error, "virtio: gpu: request out of order %d %x, %x\n", request->type, &worker, this);
                co_return Error::Code::VirtIOOperationOutOfOrder;
            }

            if(response->type != Control::OkDisplayInfo) {
                logger(LogLevel::Error, "virtio: gpu: failed to get display info\n");
                co_return Error::Code::VirtIODisplayInfo;
            }

            // const auto request_data = std::bit_cast<std::byte*>(request) + sizeof(ControlHeader);
            const auto payload = std::bit_cast<std::byte*>(response) + sizeof(ControlHeader);

            const auto& info  = *std::bit_cast<GetDisplayInfoResponse*>(payload);
            auto        found = false;
            for(auto i = 0; i < GetDisplayInfoResponse::info_size; i += 1) {
                const auto& d = info.modes[i];
                if(d.enabled == 0) {
                    continue;
                }
                display_size = {d.rect.width, d.rect.height};
                found        = true;
                break;
            }
            if(!found) {
                co_return Error::Code::VirtIODisplayInfo;
            }

            queue_data<ResourceCreate2DRequest>(control_queue, {Control::ResourceCreate2D, 0, 0, 0}, {1, Formats::B8G8R8X8Unorm, display_size[0], display_size[1]});
            queue_data<ResourceCreate2DRequest>(control_queue, {Control::ResourceCreate2D, 0, 0, 0}, {2, Formats::B8G8R8X8Unorm, display_size[0], display_size[1]});
            control_queue.notify_device();
            co_yield Success();
        }

        // create resource
        for(auto i = 0; i < 2; i += 1) {
            if(request->type != Control::ResourceCreate2D) {
                logger(LogLevel::Error, "virtio: gpu: request out of order\n");
                co_return Error::Code::VirtIOOperationOutOfOrder;
            }

            if(response->type != Control::OkNodata) {
                logger(LogLevel::Error, "virtio: gpu: failed to get create host resource %08x\n", response->type);
                co_return Error::Code::VirtIOCreateResource;
            }

            const auto request_data = std::bit_cast<std::byte*>(request) + sizeof(ControlHeader);

            const auto resource_id = std::bit_cast<ResourceCreate2DRequest*>(request_data)->resource_id;
            const auto fb_bytes    = display_size[0] * display_size[1] * 4;
            const auto fb_frames   = (fb_bytes + memory::bytes_per_frame - 1) / memory::bytes_per_frame;
            if(auto r = memory::allocate(fb_frames); !r) {
                logger(LogLevel::Error, "virtio: gpu: failed to get allocate framebuffer %d\n", r.as_error());
                co_return Error::Code::NoEnoughMemory;
            } else {
                framebuffer.first[resource_id - 1] = std::move(r.as_value());
                framebuffer.second                 = fb_frames;
            }

            const auto b  = static_cast<std::byte*>(control_queue.get_next_descriptor_buffer(sizeof(ControlHeader) + sizeof(ResourceAttachBackingRequest) + sizeof(internal::ResourceAttachBackingRequest::MemEntry)));
            const auto h  = std::bit_cast<ControlHeader*>(b + 0);
            const auto p  = std::bit_cast<ResourceAttachBackingRequest*>(b + sizeof(ControlHeader));
            *h            = {Control::ResourceAttachBacking, 0, 0, 0};
            *p            = {resource_id, 1};
            p->entries[0] = {std::bit_cast<uint64_t>(framebuffer.first[resource_id - 1]->get_frame()), fb_bytes};
            control_queue.notify_device();
            co_yield Success();
        }

        // attach backing memory
        for(auto i = 0; i < 2; i += 1) {
            if(request->type != Control::ResourceAttachBacking) {
                logger(LogLevel::Error, "virtio: gpu: request out of order\n");
                co_return Error::Code::VirtIOOperationOutOfOrder;
            }

            if(response->type != Control::OkNodata) {
                logger(LogLevel::Error, "virtio: gpu: failed to attach backing memory: %08x\n", response->type);
                co_return Error::Code::VirtIOAttachBacking;
            }

            if(i == 0) {
                co_yield Success();
            }
        }

        process::manager->post_kernel_message_with_cli(MessageType::VirtIOGPUNewDevice);
        co_yield Success();

    loop:
        switch(request->type) {
        case Control::SetScanout:
        case Control::TransferToHost2D:
        case Control::ResourceFlush:
            if(response->type != Control::OkNodata) {
                logger(LogLevel::Error, "virtio: gpu: device operation %d failed: %08x\n", request->type, response->type);
                co_return Error::Code::VirtIOProcessEvent;
            }
            break;
        default:
            logger(LogLevel::Error, "virtio: gpu: unhandled response type %x\n", request->type);
            co_return Error::Code::VirtIOUnknownEvent;
        }
        co_yield Success();
        goto loop;
    }

    auto process_control_queue() -> Error {
        auto request  = (internal::ControlHeader*)(nullptr);
        auto response = (internal::ControlHeader*)(nullptr);
        while(control_queue.read_one_buffer(std::bit_cast<void**>(&request), std::bit_cast<void**>(&response))) {
            if(request == nullptr || response == nullptr) {
                continue;
            }
            if(const auto e = worker.resume({request, response})) {
                logger(LogLevel::Error, "virtio: gpu: worker returned an error: %d\n", e.as_int());
                return e;
            }
            if(worker.done()) {
                logger(LogLevel::Error, "virtio: gpu: worker stopped\n");
                return Error::Code::VirtIOWorkerExit;
            }
            if(response->flags & internal::ControlHeader::flag_fence) {
                sync_done = 1;
                if(sync_done_event != nullptr) {
                    sync_done_event->notify();
                }
            }
        }
        return Success();
    }

    auto create_devfs_framebuffer() -> std::unique_ptr<Framebuffer> {
        auto fb = new Framebuffer(std::array{*framebuffer.first[0], *framebuffer.first[1]}, {display_size[0], display_size[1]}, control_queue, &sync_done, sync_done_event);
        return std::unique_ptr<Framebuffer>(fb);
    }

    GPUDevice(GPUDevice&) = delete;

    GPUDevice(queue::Queue control_queue, queue::Queue cursor_queue) : control_queue(std::move(control_queue)),
                                                                       cursor_queue(std::move(cursor_queue)) {
        worker.start([this]() -> Worker::Generator {
            return worker_main();
        });
        internal::queue_data(this->control_queue, {internal::Control::GetDisplayInfo, 0, 0, 0});
        this->control_queue.notify_device();
    }
};

inline auto initialize(const ::pci::Device& device) -> Result<std::unique_ptr<GPUDevice>> {
    queue::Queue::buffer_size_check<internal::buffer_size_max>();

    auto cap_addr              = device.read_register(0x34) & 0xFFu;
    auto common_config         = (volatile pci::CommonConfig*)(nullptr);
    auto notify_base           = (uint16_t*)(nullptr);
    auto notify_off_multiplier = uint32_t(0);
    auto device_config         = (volatile internal::DeviceConfig*)(nullptr);
    auto isr                   = (volatile void*)(nullptr);
    while(cap_addr != 0) {
        const auto header = device.read_capability_header(cap_addr);
        if(header.bits.cap_id != 0x09) {
            cap_addr = header.bits.next_ptr;
            continue;
        }
        const auto cap = pci::read_capability(device, cap_addr);
        switch(cap.header.bits.config_type) {
        case pci::ConfigType::Common: {
            common_config = static_cast<pci::CommonConfig*>(pci::get_config_address(device, cap));
        } break;
        case pci::ConfigType::Notify:
            notify_off_multiplier = pci::read_additional_notification_capability(device, cap_addr);
            notify_base           = static_cast<uint16_t*>(pci::get_config_address(device, cap));
            break;
        case pci::ConfigType::Device:
            device_config = static_cast<internal::DeviceConfig*>(pci::get_config_address(device, cap));
            break;
        case pci::ConfigType::ISR:
            isr = pci::get_config_address(device, cap);
            break;
        case pci::ConfigType::PCI:
            break;
        }
        cap_addr = header.bits.next_ptr;
    }
    if(common_config == nullptr || notify_base == nullptr || device_config == nullptr || isr == nullptr) {
        logger(LogLevel::Error, "virtio: gpu: device lacks capability\n");
        return Error::Code::VirtIOLegacyDevice;
    }
    const auto device_features   = common_config->read_device_features();
    auto       device_status     = device_status::acknowledge | device_status::driver;
    common_config->device_status = device_status;
    auto driver_features         = Features(0);
    if(!(device_features & features::version1)) {
        logger(LogLevel::Error, "virtio: gpu: legacy device found\n");
        return Error::Code::VirtIOLegacyDevice;
    } else {
        driver_features |= features::version1;
    }
    common_config->write_driver_features(driver_features);
    device_status |= device_status::features_ok;
    common_config->device_status = device_status;
    if(!(common_config->device_status & device_status::features_ok)) {
        logger(LogLevel::Error, "virtio: gpu: device not ready\n");
        return Error::Code::VirtIODeviceNotReady;
    }
    const auto lapic_id = lapic::read_lapic_id();

    const auto [control_queue_number, cursor_queue_number] = common_config->get_queue_number<2>();

    common_config->queue_select = control_queue_number;

    auto control_queue = queue::Queue(control_queue_number, common_config, notify_base + common_config->queue_notify_off * notify_off_multiplier);
    if(const auto error = device.configure_msix_fixed_destination(lapic_id, ::pci::MSITriggerMode::Level, ::pci::MSIDeliveryMode::Fixed, ::interrupt::Vector::VirtIOGPUControl, 0)) {
        return error;
    }
    queue::set_queue_to_config(*common_config, control_queue_number, control_queue, 0);

    common_config->queue_select = cursor_queue_number;

    auto cursor_queue = queue::Queue(cursor_queue_number, common_config, notify_base + common_config->queue_notify_off * notify_off_multiplier);
    if(const auto error = device.configure_msix_fixed_destination(lapic_id, ::pci::MSITriggerMode::Level, ::pci::MSIDeliveryMode::Fixed, ::interrupt::Vector::VirtIOGPUCursor, 1)) {
        return error;
    }
    queue::set_queue_to_config(*common_config, cursor_queue_number, cursor_queue, 1);

    device_status |= device_status::driver_ok;
    common_config->device_status = device_status;
    if(!(common_config->device_status & device_status::driver_ok)) {
        logger(LogLevel::Error, "device not ready\n");
        return Error::Code::VirtIODeviceNotReady;
    }

    return std::unique_ptr<GPUDevice>(new GPUDevice(std::move(control_queue), std::move(cursor_queue)));
}
} // namespace virtio::gpu
