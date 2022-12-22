#pragma once
#include "context.hpp"
#include "device.hpp"

namespace usb::xhci {
class DeviceManager {
  private:
    size_t          max_slots;
    Device**        devices;
    DeviceContext** device_contexts;

  public:
    auto initialize(const size_t max_slots) -> Error {
        this->max_slots = max_slots;

        devices = new(std::nothrow) Device*[max_slots + 1];
        if(devices == nullptr) {
            return Error::Code::NoEnoughMemory;
        }

        device_contexts = new(std::align_val_t{4096}, std::nothrow) DeviceContext*[max_slots + 1];
        if(!device_contexts) {
            delete[] devices;
            return Error::Code::NoEnoughMemory;
        }

        for(auto i = size_t(0); i <= max_slots; i += 1) {
            devices[i]         = nullptr;
            device_contexts[i] = nullptr;
        }

        return Error::Code::Success;
    }

    auto get_device_contexts() const -> DeviceContext** {
        return device_contexts;
    }

    auto find_by_port(const uint8_t port_number, const uint32_t route_string) const -> Device* {
        for(auto i = size_t(1); i <= max_slots; i += 1) {
            const auto dev = devices[i];
            if(dev == nullptr) {
                continue;
            }
            if(dev->get_device_context()->slot_context.bits.root_hub_port_num == port_number) {
                return dev;
            }
        }
        return nullptr;
    }

    auto find_by_state(const Device::State state) const -> Device* {
        for(auto i = size_t(1); i <= max_slots; i += 1) {
            const auto dev = devices[i];
            if(dev == nullptr) {
                continue;
            }
            if(dev->get_state() == state) {
                return dev;
            }
        }
        return nullptr;
    }

    auto find_by_slot(const uint8_t slot_id) const -> Device* {
        if(slot_id > max_slots) {
            return nullptr;
        }
        return devices[slot_id];
    }

    auto allocate_device(const uint8_t slot_id, DoorbellRegister* const doorbell_register) -> Error {
        if(slot_id > max_slots) {
            return Error::Code::InvalidSlotID;
        }

        if(devices[slot_id] != nullptr) {
            return Error::Code::AlreadyAllocated;
        }

        const auto new_device = new(std::align_val_t{64}, std::nothrow) Device(slot_id, doorbell_register);
        if(new_device == nullptr) {
            return Error::Code::NoEnoughMemory;
        }
        devices[slot_id] = new_device;
        return Error::Code::Success;
    }

    auto load_dcbaa(const uint8_t slot_id) -> Error {
        if(slot_id > max_slots) {
            return Error::Code::InvalidSlotID;
        }

        const auto dev           = devices[slot_id];
        device_contexts[slot_id] = dev->get_device_context();
        return Error::Code::Success;
    }

    auto remove(const uint8_t slot_id) -> Error {
        device_contexts[slot_id] = nullptr;
        delete devices[slot_id];
        devices[slot_id] = nullptr;
        return Error::Code::Success;
    }

    auto operator=(DeviceManager&) -> DeviceManager& = delete;

    DeviceManager(DeviceManager&) = delete;

    DeviceManager() = default;

    ~DeviceManager() {
        for(auto i = 0; i <= max_slots; i += 1) {
            delete devices[i];
        }
        delete[] devices;
        delete[] device_contexts;
    }
};
} // namespace usb::xhci
