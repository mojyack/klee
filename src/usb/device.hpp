#pragma once
#include "../error.hpp"
#include "../print.hpp"
#include "classdriver/keyboard.hpp"
#include "classdriver/mouse.hpp"
#include "descriptor.hpp"
#include "endpoint.hpp"
#include "setupdata.hpp"

namespace usb {
namespace internal {
class ConfigurationDescriptorReader {
  private:
    const uint8_t* buf;
    int            len;
    const uint8_t* p;

  public:
    auto next() -> const uint8_t* {
        p += p[0];
        return p < buf + len ? p : nullptr;
    }

    template <class T>
    auto next() -> const T* {
        while(const auto n = next()) {
            if(const auto d = descriptor_dynamic_cast<T>(n)) {
                return d;
            }
        }
        return nullptr;
    }

    ConfigurationDescriptorReader(const uint8_t* const buf, const int len) : buf(buf), len(len), p(buf) {}
};

inline auto make_endpoint_config(const EndpointDescriptor& desc) -> EndpointConfig {
    auto conf = EndpointConfig();

    conf.id = usb::EndpointID{
        desc.endpoint_address.bits.number,
        desc.endpoint_address.bits.dir_in == 1,
    };
    conf.type            = static_cast<usb::EndpointType>(desc.attributes.bits.transfer_type);
    conf.max_packet_size = desc.max_packet_size;
    conf.interval        = desc.interval;
    return conf;
}
} // namespace internal

class Device {
  private:
    std::unordered_map<SetupData, ClassDriver*, SetupData::Hasher> event_waiters;

    bool                           initialized      = false;
    int                            initialize_phase = 0;
    std::array<EndpointConfig, 16> endpoint_configs;
    size_t                         endpoint_configs_size;
    std::array<ClassDriver*, 16>   class_drivers;
    std::array<uint8_t, 256>       buffer;

    // for initialization
    uint8_t num_configurations;
    uint8_t config_index;

    auto initialize_phase1(const uint8_t* const buf, const int len) -> Error {
        const auto device_descriptor = descriptor_dynamic_cast<DeviceDescriptor>(buf);
        num_configurations           = device_descriptor->num_configurations;
        config_index                 = 0;
        initialize_phase             = 2;
        return get_descriptor(default_control_pipe_id, ConfigurationDescriptor::type, config_index, buffer.data(), buffer.size());
    }

    auto initialize_phase2(const uint8_t* const buf, const int len) -> Error {
        const auto cfg_desc = descriptor_dynamic_cast<ConfigurationDescriptor>(buf);
        if(cfg_desc == nullptr) {
            return Error::Code::InvalidDescriptor;
        }

        auto config_reader = internal::ConfigurationDescriptorReader(buf, len);
        auto class_driver  = (ClassDriver*)(nullptr);
        while(const auto if_desc = config_reader.next<InterfaceDescriptor>()) {
            class_driver = class_driver_new(*if_desc);
            if(class_driver == nullptr) {
                continue;
            }

            endpoint_configs_size = 0;
            while(endpoint_configs_size < if_desc->num_endpoints) {
                const auto ep_desc = descriptor_dynamic_cast<EndpointDescriptor>(config_reader.next());
                if(ep_desc == nullptr) {
                    continue;
                }

                const auto conf                         = internal::make_endpoint_config(*ep_desc);
                endpoint_configs[endpoint_configs_size] = conf;
                endpoint_configs_size += 1;
                class_drivers[conf.id.get_number()] = class_driver;
            }
            break;
        }

        if(class_driver == nullptr) {
            return Error::Code::Success;
        }
        initialize_phase = 3;
        return set_configuration(default_control_pipe_id, cfg_desc->configuration_value);
    }

    auto initialize_phase3(const uint8_t value) -> Error {
        for(auto i = size_t(0); i < endpoint_configs_size; i += 1) {
            class_drivers[endpoint_configs[i].id.get_number()]->set_endpoint(endpoint_configs[i]);
        }
        initialize_phase = 4;
        initialized      = true;
        return Error::Code::Success;
    }

    auto get_descriptor(const EndpointID id, const uint8_t type, const uint8_t index, void* const buf, const int len) -> Error {
        auto setup_data                        = SetupData();
        setup_data.request_type.bits.direction = static_cast<uint8_t>(DirectionRequestType::In);
        setup_data.request_type.bits.type      = static_cast<uint8_t>(TypeRequestType::Standard);
        setup_data.request_type.bits.recipient = static_cast<uint8_t>(RecipientRequestType::Device);
        setup_data.request                     = static_cast<uint8_t>(Request::GetDescriptor);
        setup_data.value                       = (static_cast<uint16_t>(type) << 8) | index;
        setup_data.index                       = 0;
        setup_data.length                      = len;
        return control_in(id, setup_data, buf, len, nullptr);
    }

    auto class_driver_new(const InterfaceDescriptor& desc) -> ClassDriver* {
        switch(desc.interface_class) {
        case 3:
            switch(desc.interface_sub_class) {
            case 1:
                // HID boot protocol
                switch(desc.interface_protocol) {
                case 1: {
                    // keyboard
                    const auto keyboard_driver = new usb::HIDKeyboardDriver(this, desc.interface_number);
                    if(usb::HIDKeyboardDriver::default_observer) {
                        keyboard_driver->subscribe_keypush(usb::HIDKeyboardDriver::default_observer);
                    }
                    return keyboard_driver;
                } break;
                case 2: {
                    // mouse
                    const auto mouse_driver = new usb::HIDMouseDriver(this, desc.interface_number);
                    if(usb::HIDMouseDriver::default_observer) {
                        mouse_driver->subscribe_mousemove(usb::HIDMouseDriver::default_observer);
                    }
                    return mouse_driver;
                } break;
                }
                break;
            }
            break;
        }
        return nullptr;
    }

    auto set_configuration(const EndpointID id, const uint8_t config_value) -> Error {
        auto setup_data                        = SetupData();
        setup_data.request_type.bits.direction = static_cast<uint8_t>(DirectionRequestType::Out);
        setup_data.request_type.bits.type      = static_cast<uint8_t>(TypeRequestType::Standard);
        setup_data.request_type.bits.recipient = static_cast<uint8_t>(RecipientRequestType::Device);
        setup_data.request                     = static_cast<uint8_t>(Request::SetConfiguration);
        setup_data.value                       = config_value;
        setup_data.index                       = 0;
        setup_data.length                      = 0;
        return control_out(id, setup_data, nullptr, 0, nullptr);
    }

  protected:
    auto on_control_completed(const EndpointID id, const SetupData setup_data, const void* const buf, const int len) -> Error {
        if(initialized) {
            if(const auto p = event_waiters.find(setup_data); p != event_waiters.end()) {
                return p->second->on_control_completed(id, setup_data, buf, len);
            }
            return Error::Code::NoWaiter;
        }

        const auto& buf8 = reinterpret_cast<const uint8_t*>(buf);
        switch(initialize_phase) {
        case 1:
            if(setup_data.request == static_cast<uint8_t>(Request::GetDescriptor) && descriptor_dynamic_cast<DeviceDescriptor>(buf8) != nullptr) {
                return initialize_phase1(buf8, len);
            }
            return Error::Code::InvalidPhase;
        case 2:
            if(setup_data.request == static_cast<uint8_t>(Request::GetDescriptor) && descriptor_dynamic_cast<ConfigurationDescriptor>(buf8) != nullptr) {
                return initialize_phase2(buf8, len);
            }
            return Error::Code::InvalidPhase;
        case 3:
            if(setup_data.request == static_cast<uint8_t>(Request::SetConfiguration)) {
                return initialize_phase3(setup_data.value & 0xFFu);
            }
            return Error::Code::InvalidPhase;
        }

        return Error::Code::NotImplemented;
    }

    auto on_interrupt_completed(const EndpointID id, const void* const buf, const int len) -> Error {
        if(const auto w = class_drivers[id.get_number()]) {
            return w->on_interrupt_completed(id, buf, len);
        }
        return Error::Code::NoWaiter;
    }

  public:
    virtual auto control_in(const EndpointID id, const SetupData setup_data, void* const buf, const int len, ClassDriver* const issuer) -> Error {
        if(issuer != nullptr) {
            event_waiters[setup_data] = issuer;
        }
        return Error::Code::Success;
    }

    virtual auto control_out(const EndpointID id, const SetupData setup_data, void* const buf, const int len, ClassDriver* const issuer) -> Error {
        if(issuer != nullptr) {
            event_waiters[setup_data] = issuer;
        }
        return Error::Code::Success;
    }

    virtual auto interrupt_in(const EndpointID id, void* const buf, const int len) -> Error {
        return Error::Code::Success;
    }

    virtual auto interrupt_out(const EndpointID id, void* const buf, const int len) -> Error {
        return Error::Code::Success;
    }

    auto start_initializing() -> Error {
        initialized      = false;
        initialize_phase = 1;
        return get_descriptor(default_control_pipe_id, DeviceDescriptor::type, 0, buffer.data(), buffer.size());
    }

    auto is_initialized() const -> bool {
        return initialized;
    }

    auto get_endpoint_configs() const -> std::pair<size_t, const EndpointConfig*> {
        return {endpoint_configs_size, endpoint_configs.data()};
    }

    auto on_endpoint_configured() -> Error {
        for(const auto c : class_drivers) {
            if(c == nullptr) {
                continue;
            }
            if(const auto error = c->on_endpoint_configured()) {
                return error;
            }
        }
        return Error::Code::Success;
    }

    Device() {
        for(auto& c : class_drivers) {
            c = nullptr;
        }
    }

    virtual ~Device() = default;
};
} // namespace usb
