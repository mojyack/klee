#pragma once
#include "../memory.hpp"
#include "context.hpp"
#include "device.hpp"

namespace usb::xhci {
class DeviceManager {
  private:
    DeviceContext** device_context_pointers;
    size_t          max_slots;
    Device**        devices;

  public:
    auto initialize(const size_t max_slots) -> Error {
        this->max_slots = max_slots;

        devices = allocate_array<Device*>(max_slots + 1, 0, 0);
        if(devices == nullptr) {
            return Error::Code::NoEnoughMemory;
        }

        device_context_pointers = allocate_array<DeviceContext*>(max_slots + 1, 64, 4096);
        if(device_context_pointers == nullptr) {
            deallocate_memory(devices);
            return Error::Code::NoEnoughMemory;
        }

        for(auto i = size_t(0); i <= max_slots; i += 1) {
            devices[i]                 = nullptr;
            device_context_pointers[i] = nullptr;
        }

        return Error::Code::Success;
    }

    auto get_device_contexts() const -> DeviceContext** {
        return device_context_pointers;
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

        devices[slot_id] = allocate_array<Device>(1, 64, 4096);
        new(devices[slot_id]) Device(slot_id, doorbell_register);
        return Error::Code::Success;
    }

    auto load_dcbaa(const uint8_t slot_id) -> Error {
        if(slot_id > max_slots) {
            return Error::Code::InvalidSlotID;
        }

        const auto dev                   = devices[slot_id];
        device_context_pointers[slot_id] = dev->get_device_context();
        return Error::Code::Success;
    }

    auto remove(const uint8_t slot_id) -> Error {
        device_context_pointers[slot_id] = nullptr;
        deallocate_memory(devices[slot_id]);
        devices[slot_id] = nullptr;
        return Error::Code::Success;
    }
};
} // namespace usb::xhci
