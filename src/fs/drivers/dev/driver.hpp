#pragma once
#include "../../../util/string-map.hpp"
#include "block.hpp"
#include "fb.hpp"
#include "keyboard.hpp"

namespace fs::dev {
class Driver : public fs::Driver {
  private:
    FileAbstractWithDriverData root;

    StringMap<std::unique_ptr<Device>> devices;

  public:
    auto read(const uint64_t fop_data, uint64_t& handle_data, const size_t block, const size_t count, void* const buffer) -> Result<size_t> override {
        if(fop_data == 0) {
            return Error::Code::NotFile;
        }

        auto& device = *std::bit_cast<Device*>(fop_data);
        return device.read(handle_data, block, count, buffer);
    }

    auto write(const uint64_t fop_data, uint64_t& handle_data, const size_t block, const size_t count, const void* const buffer) -> Result<size_t> override {
        if(fop_data == 0) {
            return Error::Code::NotFile;
        }

        auto& device = *std::bit_cast<Device*>(fop_data);
        return device.write(handle_data, block, count, buffer);
    }

    auto find(const uint64_t fop_data, uint64_t& handle_data, const std::string_view name) -> Result<FileAbstractWithDriverData> override {
        if(fop_data != 0) {
            return Error::Code::NotDirectory;
        }

        if(const auto p = devices.find(name); p != devices.end()) {
            auto& name   = p->first;
            auto& device = *p->second;
            return FileAbstractWithDriverData{{name, device.get_filesize(), FileType::Device, device.get_blocksize_exp(), device.get_attributes()}, std::bit_cast<uint64_t>(&device)};
        } else {
            return Error::Code::NoSuchFile;
        }
    }

    auto create(const uint64_t fop_data, uint64_t& handle_data, const std::string_view name, const FileType type) -> Result<FileAbstractWithDriverData> override {
        return Error::Code::NotSupported;
    }

    auto readdir(const uint64_t fop_data, uint64_t& handle_data, const size_t index) -> Result<FileAbstractWithDriverData> override {
        if(fop_data != 0) {
            return Error::Code::NotDirectory;
        }

        if(index >= devices.size()) {
            return Error::Code::EndOfFile;
        }

        const auto p      = std::next(devices.begin(), index);
        auto&      name   = p->first;
        auto&      device = *p->second;
        return FileAbstractWithDriverData{{name, device.get_filesize(), FileType::Device, device.get_blocksize_exp(), device.get_attributes()}, std::bit_cast<uint64_t>(&device)};
    }

    auto remove(const uint64_t fop_data, uint64_t& handle_data, const std::string_view name) -> Error override {
        if(fop_data != 0) {
            return Error::Code::NotDirectory;
        }

        if(const auto p = devices.find(name); p != devices.end()) {
            devices.erase(p);
            return Success();
        } else {
            return Error::Code::NoSuchFile;
        }
    }

    auto get_device_type(const uint64_t fop_data) -> DeviceType override {
        auto& device = *std::bit_cast<Device*>(fop_data);
        return device.get_device_type();
    }

    auto create_device(const uint64_t fop_data, uint64_t& handle_data, const std::string_view name, const uintptr_t device_impl) -> Result<FileAbstractWithDriverData> override {
        if(fop_data != 0) {
            return Error::Code::NotDirectory;
        }

        if(devices.find(name) != devices.end()) {
            return Error::Code::FileExists;
        }

        const auto device = std::bit_cast<Device*>(device_impl);
        const auto p      = devices.emplace(name, device).first;
        return FileAbstractWithDriverData{{p->first, device->get_filesize(), FileType::Device, device->get_blocksize_exp(), device->get_attributes()}, std::bit_cast<uint64_t>(&device)};
    }

    auto control_device(const uint64_t fop_data, uint64_t& handle_data, const DeviceOperation op, void* const arg) -> Error override {
        if(fop_data == 0) {
            return Error::Code::NotFile;
        }

        auto& device = *std::bit_cast<Device*>(fop_data);
        switch(device.get_device_type()) {
        case DeviceType::None:
            return Error::Code::InvalidDeviceType;
        case DeviceType::Framebuffer: {
            auto& fb = *std::bit_cast<FramebufferDevice*>(&device);
            switch(op) {
            case DeviceOperation::GetSize: {
                *std::bit_cast<std::array<size_t, 2>*>(arg) = fb.get_size();
            } break;
            case DeviceOperation::GetDirectPointer: {
                *std::bit_cast<std::byte***>(arg) = fb.direct_access();
            } break;
            case DeviceOperation::Swap: {
                fb.swap();
            } break;
            case DeviceOperation::IsDoubleBuffered: {
                *std::bit_cast<bool*>(arg) = fb.is_double_buffered();
            } break;
            default:
                return Error::Code::InvalidDeviceOperation;
            }
        } break;
        case DeviceType::Keyboard: {
            // auto& kb = *std::bit_cast<KeyboardDevice*>(&device);
            switch(op) {
            default:
                return Error::Code::InvalidDeviceOperation;
            }
        } break;
        case DeviceType::Mouse: {
        } break;
        case DeviceType::Block: {
            auto& block = *std::bit_cast<BlockDevice*>(&device);
            switch(op) {
            case DeviceOperation::GetBytesPerSector:
                *std::bit_cast<size_t*>(arg) = block.get_bytes_per_sector();
                break;
            default:
                return Error::Code::InvalidDeviceOperation;
            }
        } break;
        }
        return Success();
    }

    auto create_handle_data(const uint64_t fop_data) -> Result<uint64_t> override {
        if(fop_data != 0) {
            return 0;
        }

        auto& device = *std::bit_cast<Device*>(fop_data);
        return device.create_handle_data();
    }

    auto destroy_handle_data(const uint64_t fop_data, uint64_t& handle_data) -> Error override {
        if(fop_data != 0) {
            return Success();
        }

        auto& device = *std::bit_cast<Device*>(fop_data);
        return device.destroy_handle_data(handle_data);
    }

    auto on_handle_create(const uint64_t fop_data, uint64_t& handle_data) -> void override {
        if(fop_data == 0) {
            return;
        }

        auto& device = *std::bit_cast<Device*>(fop_data);
        device.on_handle_create(handle_data);
    }

    auto on_handle_destroy(const uint64_t fop_data, uint64_t& handle_data) -> void override {
        if(fop_data == 0) {
            return;
        }

        auto& device = *std::bit_cast<Device*>(fop_data);
        device.on_handle_destroy(handle_data);
    }

    auto get_write_event(const uint64_t fop_data, uint64_t& handle_data) -> Event* override {
        if(fop_data == 0) {
            return nullptr;
        }

        auto& device = *std::bit_cast<Device*>(fop_data);
        return device.get_write_event(handle_data);
    }

    auto get_cache_provider(const uint64_t fop_data) -> std::shared_ptr<CacheProvider> override {
        if(fop_data == 0) {
            return nullptr;
        }

        auto& device = *std::bit_cast<Device*>(fop_data);
        return device.get_cache_provider();
    }

    auto get_root() -> FileAbstractWithDriverData& override {
        return root;
    }

    Driver() : root{{"/", 0, FileType::Directory, 0, fs::volume_root_attributes}, 0} {
    }
};

inline auto new_driver() -> Driver {
    return Driver();
}
} // namespace fs::dev
