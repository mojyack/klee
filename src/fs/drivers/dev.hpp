#pragma once
#include "../fs.hpp"

namespace fs::dev {
class Device {
  public:
    virtual auto get_device_type() const -> DeviceType = 0;
    virtual ~Device() {}
};

class FramebufferDevice : public Device {
  protected:
    uint8_t*              data;
    std::array<size_t, 2> size;

  public:
    auto get_device_type() const -> DeviceType override {
        return DeviceType::Framebuffer;
    }

    auto get_size() -> std::array<size_t, 2> {
        return size;
    }

    virtual auto swap() -> void = 0;
};

class Driver : public fs::Driver {
  private:
    OpenInfo root;

    std::unordered_map<std::string, std::unique_ptr<Device>> devices;

  public:
    auto read(DriverData data, size_t offset, size_t size, void* buffer) -> Error override {
        return Error::Code::InvalidData;
    }

    auto write(DriverData data, size_t offset, size_t size, const void* buffer) -> Error override {
        return Error::Code::InvalidData;
    }

    auto find(const DriverData data, const std::string_view name) -> Result<OpenInfo> override {
        if(data.num != 0) {
            return Error::Code::InvalidData;
        }

        if(const auto p = devices.find(std::string(name)); p != devices.end()) {
            return OpenInfo(p->first, *this, p->second.get(), FileType::Device, 0);
        } else {
            return Error::Code::NoSuchFile;
        }
    }

    auto create(const DriverData data, const std::string_view name, const FileType type) -> Result<OpenInfo> override {
        return Error::Code::InvalidData;
    }

    auto readdir(const DriverData data, const size_t index) -> Result<OpenInfo> override {
        if(data.num != 0) {
            return Error::Code::InvalidData;
        }
        if(index >= devices.size()) {
            return Error::Code::EndOfFile;
        }

        const auto p = std::next(devices.begin(), index);
        return OpenInfo(p->first, *this, p->second.get(), FileType::Device, 0);
    }

    auto remove(const DriverData data, const std::string_view name) -> Error override {
        if(data.num != 0) {
            return Error::Code::InvalidData;
        }
        if(const auto p = devices.find(std::string(name)); p != devices.end()) {
            devices.erase(p);
            return Error();
        } else {
            return Error::Code::NoSuchFile;
        }
    }

    auto get_device_type(const DriverData data) -> DeviceType override {
        auto& device = *reinterpret_cast<Device*>(data.num);
        return device.get_device_type();
    }

    auto create_device(const DriverData data, const std::string_view name, const DeviceType device_type, const uint64_t driver_impl) -> Result<OpenInfo> override {
        if(devices.find(std::string(name)) != devices.end()) {
            return Error::Code::FileExists;
        }

        auto device = (Device*)(nullptr);
        switch(device_type) {
        case DeviceType::None:
            break;
        case DeviceType::Framebuffer:
            device = reinterpret_cast<FramebufferDevice*>(driver_impl);
            break;
        }
        if(device == nullptr) {
            return Error::Code::InvalidDeviceType;
        }
        const auto p = devices.emplace(name, device).first;
        return OpenInfo(p->first, *this, p->second.get(), FileType::Device, 0);
    }

    auto control_device(const DriverData data, const DeviceOperation op, void* const arg) -> Error override {
        auto& device = *reinterpret_cast<Device*>(data.num);
        switch(device.get_device_type()) {
        case DeviceType::None:
            return Error::Code::InvalidDeviceType;
        case DeviceType::Framebuffer: {
            auto& fb = *reinterpret_cast<FramebufferDevice*>(&device);
            switch(op) {
            case DeviceOperation::GetSize: {
                *reinterpret_cast<std::array<size_t, 2>*>(arg) = fb.get_size();
            } break;
            case DeviceOperation::Swap: {
                fb.swap();
            } break;
            }
        } break;
        }
        return Error();
    }

    auto get_root() -> OpenInfo& override {
        return root;
    }

    Driver() : root("/", *this, nullptr, FileType::Directory, 0, true) {}
};

inline auto new_driver() -> Driver {
    return Driver();
}
} // namespace fs::dev
