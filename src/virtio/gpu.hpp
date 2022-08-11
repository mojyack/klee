#pragma once
#include <algorithm>

#include "../framebuffer.hpp"
#include "../interrupt/vector.hpp"
#include "../log.hpp"
#include "../pci.hpp"
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

class Framebuffer : public ::Framebuffer {
  private:
    std::array<FrameID, 2>  buffers;
    std::array<uint32_t, 2> display_size;
    queue::Queue*           control_queue;
    uint32_t*               sync_done;

    auto find_pointer(const Point point, const bool flip) -> uint8_t* override {
        const auto base = static_cast<uint8_t*>(buffers[flip].get_frame());
        return base + (point.y * display_size[0] + point.x) * 4;
    }

    auto do_swap(const bool flip) -> bool override {
        if(*sync_done == 0) {
            return false;
        }
        *sync_done = 0;

        const auto resource_id = uint32_t(!flip ? 1 : 2);
        internal::queue_data<internal::TransferToHost2DRequest>(*control_queue, {internal::Control::TransferToHost2D, 0, 0, 0}, {{0, 0, display_size[0], display_size[1]}, 0, resource_id, 0});
        internal::queue_data<internal::ResourceFlushRequest>(*control_queue, {internal::Control::ResourceFlush, 0, 0, 0}, {{0, 0, display_size[0], display_size[1]}, resource_id, 0});
        internal::queue_data<internal::SetScanoutRequest>(*control_queue, {internal::Control::SetScanout, internal::ControlHeader::flag_fence, 1, 0}, {{0, 0, display_size[0], display_size[1]}, 0, resource_id});
        control_queue->notify_device();
        return true;
    }

  public:
    auto get_size() const -> std::array<size_t, 2> override {
        return {display_size[0], display_size[1]};
    }

    Framebuffer(const std::array<FrameID, 2> buffers, const std::array<uint32_t, 2> display_size, queue::Queue& control_queue, uint32_t* const sync_done) : buffers(buffers),
                                                                                                                                                            display_size(display_size),
                                                                                                                                                            control_queue(&control_queue),
                                                                                                                                                            sync_done(sync_done){};
};

class GPUDevice {
  private:
    queue::Queue control_queue;
    queue::Queue cursor_queue;

    enum class SetupStage {
        Init,
        DisplayInfo,
        HostResource1,
        HostResource,
        Attach1,
        Attach,
    };

    SetupStage setup_stage = SetupStage::Init;

    std::array<uint32_t, 2>                          display_size = {1024, 768};
    std::pair<std::array<SmartFrameID, 2>, uint64_t> framebuffer;
    std::optional<Framebuffer>                       virtio_framebuffer;
    uint32_t                                         sync_done = 1;

  public:
    auto process_control_queue() -> Error {
        auto request  = (internal::ControlHeader*)(nullptr);
        auto response = (internal::ControlHeader*)(nullptr);
        while(control_queue.read_one_buffer(reinterpret_cast<void**>(&request), reinterpret_cast<void**>(&response))) {
            if(request == nullptr || response == nullptr) {
                continue;
            }
            const auto request_data = reinterpret_cast<uint8_t*>(request) + sizeof(internal::ControlHeader);
            const auto payload      = reinterpret_cast<uint8_t*>(response) + sizeof(internal::ControlHeader);
            switch(request->type) {
            case internal::Control::GetDisplayInfo: {
                if(response->type != internal::Control::OkDisplayInfo) {
                    logger(LogLevel::Error, "failed to get display info\n");
                    break;
                }
                if(setup_stage != SetupStage::Init) {
                    break;
                }
                const auto& info = *reinterpret_cast<internal::GetDisplayInfoResponse*>(payload);
                for(auto i = 0; i < internal::GetDisplayInfoResponse::info_size; i += 1) {
                    const auto& d = info.modes[i];
                    if(d.enabled == 0) {
                        continue;
                    }
                    display_size = {d.rect.width, d.rect.height};
                    setup_stage  = SetupStage::DisplayInfo;
                    break;
                }

                if(setup_stage != SetupStage::DisplayInfo) {
                    break;
                }

                internal::queue_data<internal::ResourceCreate2DRequest>(control_queue, {internal::Control::ResourceCreate2D, 0, 0, 0}, {1, internal::Formats::B8G8R8X8Unorm, display_size[0], display_size[1]});
                internal::queue_data<internal::ResourceCreate2DRequest>(control_queue, {internal::Control::ResourceCreate2D, 0, 0, 0}, {2, internal::Formats::B8G8R8X8Unorm, display_size[0], display_size[1]});
                control_queue.notify_device();
            } break;
            case internal::Control::ResourceCreate2D: {
                if(response->type != internal::Control::OkNodata) {
                    logger(LogLevel::Error, "failed to get create host resource %08x\n", response->type);
                    break;
                }

                if(setup_stage == SetupStage::DisplayInfo) {
                    setup_stage = SetupStage::HostResource1;
                } else if(setup_stage == SetupStage::HostResource1) {
                    setup_stage = SetupStage::HostResource;
                } else {
                    break;
                }

                const auto resource_id = reinterpret_cast<internal::ResourceCreate2DRequest*>(request_data)->resource_id;
                const auto fb_bytes    = display_size[0] * display_size[1] * 4;
                const auto fb_frames   = (fb_bytes + bytes_per_frame - 1) / bytes_per_frame;
                if(const auto f = allocator->allocate(fb_frames)) {
                    framebuffer.first[resource_id - 1] = SmartFrameID(f.as_value(), fb_frames);
                    framebuffer.second                 = fb_frames;
                } else {
                    logger(LogLevel::Error, "failed to get allocate framebuffer %d\n", f.as_error());
                }

                const auto b  = static_cast<uint8_t*>(control_queue.get_next_descriptor_buffer(sizeof(internal::ControlHeader) + sizeof(internal::ResourceAttachBackingRequest) + sizeof(internal::ResourceAttachBackingRequest::MemEntry)));
                const auto h  = reinterpret_cast<internal::ControlHeader*>(b + 0);
                const auto p  = reinterpret_cast<internal::ResourceAttachBackingRequest*>(b + sizeof(internal::ControlHeader));
                *h            = {internal::Control::ResourceAttachBacking, 0, 0, 0};
                *p            = {resource_id, 1};
                p->entries[0] = {reinterpret_cast<uint64_t>(framebuffer.first[resource_id - 1]->get_frame()), fb_bytes};
                control_queue.notify_device();
                break;
            }
            case internal::Control::ResourceAttachBacking: {
                if(response->type != internal::Control::OkNodata) {
                    logger(LogLevel::Error, "failed to get attach backing memory %08x\n", response->type);
                    break;
                }

                if(setup_stage == SetupStage::HostResource) {
                    setup_stage = SetupStage::Attach1;
                } else if(setup_stage == SetupStage::Attach1) {
                    setup_stage = SetupStage::Attach;
                } else {
                    break;
                }
                if(setup_stage != SetupStage::Attach) {
                    break;
                }
                virtio_framebuffer.emplace(std::array{*framebuffer.first[0], *framebuffer.first[1]}, display_size, control_queue, &sync_done);
                auto [w, h]   = ::framebuffer->get_size();
                ::framebuffer = &virtio_framebuffer.value();
                if(w != display_size[0] || w != display_size[1]) {
                    task::kernel_task->send_message(MessageType::ScreenResized);
                }
            } break;
            case internal::Control::SetScanout:
            case internal::Control::TransferToHost2D:
            case internal::Control::ResourceFlush:
                if(response->type != internal::Control::OkNodata) {
                    logger(LogLevel::Error, "device operation %d failed %08x\n", request->type, response->type);
                    break;
                }
                break;
            default:
                logger(LogLevel::Error, "unhandled response type %x\n", request->type);
                break;
            }
            if(response->flags & internal::ControlHeader::flag_fence) {
                sync_done = 1;
                task::kernel_task->send_message(MessageType::RefreshScreenDone);
            }
        }
        return Error::Code::Success;
    }

    GPUDevice()            = default;
    GPUDevice(GPUDevice&&) = default;
    GPUDevice(queue::Queue control_queue, queue::Queue cursor_queue) : control_queue(std::move(control_queue)),
                                                                       cursor_queue(std::move(cursor_queue)) {
        internal::queue_data(this->control_queue, {internal::Control::GetDisplayInfo, 0, 0, 0});
        this->control_queue.notify_device();
    }
};

inline auto initialize(const ::pci::Device& device) -> Result<GPUDevice> {
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
        logger(LogLevel::Error, "the device lacks capability\n");
        return Error::Code::VirtIOLegacyDevice;
    }
    const auto device_features   = common_config->read_device_features();
    auto       device_status     = device_status::acknowledge | device_status::driver;
    common_config->device_status = device_status;
    auto driver_features         = Features(0);
    if(!(device_features & features::version1)) {
        logger(LogLevel::Error, "legacy device found\n");
        return Error::Code::VirtIOLegacyDevice;
    } else {
        driver_features |= features::version1;
    }
    common_config->write_driver_features(driver_features);
    device_status |= device_status::features_ok;
    common_config->device_status = device_status;
    if(!(common_config->device_status & device_status::features_ok)) {
        logger(LogLevel::Error, "device not ready\n");
        return Error::Code::VirtIODeviceNotReady;
    }
    const auto bsp_local_apic_id = *reinterpret_cast<const uint32_t*>(0xFEE00020) >> 24;

    const auto [control_queue_number, cursor_queue_number] = common_config->get_queue_number<2>();

    common_config->queue_select   = control_queue_number;
    const auto control_queue_size = common_config->queue_size;

    auto control_queue = queue::Queue(control_queue_size, control_queue_number, &common_config->queue_select, notify_base + common_config->queue_notify_off * notify_off_multiplier);
    if(const auto error = device.configure_msix_fixed_destination(bsp_local_apic_id, ::pci::MSITriggerMode::Level, ::pci::MSIDeliveryMode::Fixed, ::interrupt::Vector::VirtIOGPUControl, 0)) {
        return error;
    }
    common_config->set_queue(control_queue_number, control_queue, 0);

    common_config->queue_select  = cursor_queue_number;
    const auto cursor_queue_size = common_config->queue_size;
    auto       cursor_queue      = queue::Queue(cursor_queue_size, cursor_queue_number, &common_config->queue_select, notify_base + common_config->queue_notify_off * notify_off_multiplier);
    if(const auto error = device.configure_msix_fixed_destination(bsp_local_apic_id, ::pci::MSITriggerMode::Level, ::pci::MSIDeliveryMode::Fixed, ::interrupt::Vector::VirtIOGPUCursor, 1)) {
        return error;
    }
    common_config->set_queue(cursor_queue_number, cursor_queue, 1);

    device_status |= device_status::driver_ok;
    common_config->device_status = device_status;
    if(!(common_config->device_status & device_status::driver_ok)) {
        logger(LogLevel::Error, "device not ready\n");
        return Error::Code::VirtIODeviceNotReady;
    }

    return GPUDevice(std::move(control_queue), std::move(cursor_queue));
}
} // namespace virtio::gpu
