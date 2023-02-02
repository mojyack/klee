#pragma once
#include "../../mutex.hpp"
#include "../fs.hpp"

namespace fs::dev {
class Device {
  public:
    virtual auto read(size_t offset, size_t size, void* buffer) -> Result<size_t>        = 0;
    virtual auto write(size_t offset, size_t size, const void* buffer) -> Result<size_t> = 0;

    virtual auto get_filesize() const -> size_t {
        return 0;
    }

    virtual auto get_device_type() const -> DeviceType = 0;
    virtual auto on_handle_create(Event& write_event) -> void {}
    virtual auto on_handle_destroy() -> void {}

    virtual ~Device() {}
};

class FramebufferDevice : public Device {
  protected:
    uint8_t*              data;
    std::array<size_t, 2> buffer_size;
    Event*                write_event;

  public:
    auto read(const size_t offset, const size_t size, void* const buffer) -> Result<size_t> override {
        if(offset + size >= buffer_size[0] * buffer_size[1] * 4) {
            return Error::Code::EndOfFile;
        }
        memcpy(buffer, data + offset, size);
        return size_t(size);
    }

    auto write(const size_t offset, const size_t size, const void* const buffer) -> Result<size_t> override {
        if(offset + size >= buffer_size[0] * buffer_size[1] * 4) {
            return Error::Code::EndOfFile;
        }
        memcpy(data + offset, buffer, size);
        return size_t(size);
    }

    auto get_device_type() const -> DeviceType override {
        return DeviceType::Framebuffer;
    }

    auto on_handle_create(Event& write_event) -> void override {
        this->write_event = &write_event;
    }

    auto on_handle_destroy() -> void override {
        this->write_event = nullptr;
    }

    auto get_size() -> std::array<size_t, 2> {
        return buffer_size;
    }

    auto direct_access() -> uint8_t** {
        return &data;
    }

    virtual auto swap() -> void                     = 0;
    virtual auto is_double_buffered() const -> bool = 0;
};

struct KeyboardPacket {
    uint8_t keycode;
    uint8_t modifier;
    char    ascii;
};

class KeyboardDevice : public Device {
  private:
    bool                                  active = false;
    Critical<std::vector<KeyboardPacket>> packets;
    Event*                                write_event;

  protected:
    auto push_packet(const KeyboardPacket packet) -> void {
        if(!active) {
            return;
        }

        {
            auto [lock, pks] = packets.access();
            pks.emplace_back(packet);
        }
        write_event->notify();
    }

  public:
    auto read(const size_t offset, const size_t size, void* const buffer) -> Result<size_t> override {
        if(offset != 0) {
            return Error::Code::IndexOutOfRange;
        }
        if(size == 0 || size % sizeof(KeyboardPacket) != 0) {
            return Error::Code::InvalidSize;
        }

        auto copy_num = size_t();
        while(true) {
            {
                auto [lock, pks] = packets.access();
                copy_num         = std::min(size / sizeof(KeyboardPacket), pks.size());
            }

            if(copy_num != 0) {
                break;
            }
            write_event->wait();
            write_event->reset();
        }

        auto [lock, pks]      = packets.access();
        const auto copy_bytes = sizeof(KeyboardPacket) * copy_num;
        memcpy(buffer, pks.data(), copy_bytes);
        pks.erase(pks.begin(), pks.begin() + copy_num);
        return size_t(copy_bytes);
    }

    auto write(const size_t offset, const size_t size, const void* const buffer) -> Result<size_t> override {
        return Error::Code::InvalidDeviceOperation;
    }

    auto get_device_type() const -> DeviceType override {
        return DeviceType::Keyboard;
    }

    auto on_handle_create(Event& write_event) -> void override {
        active            = true;
        this->write_event = &write_event;
    }

    auto on_handle_destroy() -> void override {
        active = false;
        packets.access().second.clear();
    }
};

class BlockDevice : public Device {
  protected:
    size_t bytes_per_sector;
    size_t total_sectors;

  public:
    virtual auto read_sector(size_t sector, size_t count, void* buffer) -> Error        = 0;
    virtual auto write_sector(size_t sector, size_t count, const void* buffer) -> Error = 0;

    auto read(const size_t offset, size_t size, void* buffer_) -> Result<size_t> override {
        const auto total_size = size;
        auto       sector     = offset / bytes_per_sector;
        auto       buffer     = static_cast<uint8_t*>(buffer_);

        if(offset % bytes_per_sector == 0 && size % bytes_per_sector == 0) {
            if(const auto e = read_sector(sector, size / bytes_per_sector, buffer)) {
                return e;
            } else {
                return size;
            }
        }

        auto read_buffer = std::vector<uint8_t>(bytes_per_sector);

        {
            const auto offset_in_sector = offset % bytes_per_sector;
            const auto size_in_sector   = bytes_per_sector - offset_in_sector;
            const auto copy_len         = std::min(size, size_in_sector);
            error_or(read_sector(sector, 1, read_buffer.data()));
            memcpy(buffer, read_buffer.data() + offset_in_sector, copy_len);
            buffer += copy_len;
            size -= copy_len;
            sector += 1;
        }

        if(size != 0) {
            const auto count = size / bytes_per_sector;
            error_or(read_sector(sector, count, buffer));
            buffer += bytes_per_sector * count;
            size -= bytes_per_sector * count;
            sector += count;
        }

        if(size != 0) {
            error_or(read_sector(sector, 1, read_buffer.data()));
            memcpy(buffer, read_buffer.data(), size);
        }

        return size_t(total_size);
    }

    auto write(size_t offset, size_t size, const void* buffer_) -> Result<size_t> override {
        const auto total_size = size;
        auto       sector     = offset / bytes_per_sector;
        auto       buffer     = static_cast<const uint8_t*>(buffer_);

        if(offset % bytes_per_sector == 0 && size % bytes_per_sector == 0) {
            if(const auto e = write_sector(sector, size / bytes_per_sector, buffer)) {
                return e;
            } else {
                return size;
            }
        }

        auto sector_buffer = std::vector<uint8_t>(bytes_per_sector);

        {
            error_or(read_sector(sector, 1, sector_buffer.data()));
            const auto offset_in_sector = offset % bytes_per_sector;
            const auto size_in_sector   = bytes_per_sector - offset_in_sector;
            const auto copy_len         = std::min(size, size_in_sector);
            memcpy(sector_buffer.data() + offset_in_sector, buffer, copy_len);
            error_or(write_sector(sector, 1, sector_buffer.data()));
            buffer += copy_len;
            size -= copy_len;
            sector += 1;
        }

        if(size != 0) {
            const auto count = size / bytes_per_sector;
            error_or(write_sector(sector, count, buffer));
            buffer += bytes_per_sector * count;
            size -= bytes_per_sector * count;
            sector += count;
        }

        if(size != 0) {
            error_or(read_sector(sector, 1, sector_buffer.data()));
            memcpy(sector_buffer.data(), buffer, size);
            error_or(write_sector(sector, 1, sector_buffer.data()));
        }

        return size_t(total_size);
    }

    auto get_bytes_per_sector() const -> size_t {
        return bytes_per_sector;
    }

    auto get_filesize() const -> size_t override {
        return bytes_per_sector * total_sectors;
    }

    auto get_device_type() const -> DeviceType override {
        return DeviceType::Block;
    }
};

class Driver : public fs::Driver {
  private:
    OpenInfo root;

    std::unordered_map<std::string, std::unique_ptr<Device>> devices;

  public:
    auto read(OpenInfo& info, size_t offset, size_t size, void* buffer) -> Result<size_t> override {
        if(info.get_driver_data() == 0) {
            return Error::Code::NotFile;
        }

        auto& device = *reinterpret_cast<Device*>(info.get_driver_data());
        return device.read(offset, size, buffer);
    }

    auto write(OpenInfo& info, const size_t offset, const size_t size, const void* const buffer) -> Result<size_t> override {
        if(info.get_driver_data() == 0) {
            return Error::Code::NotFile;
        }

        auto& device = *reinterpret_cast<Device*>(info.get_driver_data());
        return device.write(offset, size, buffer);
    }

    auto find(OpenInfo& info, const std::string_view name) -> Result<OpenInfo> override {
        if(info.get_driver_data() != 0) {
            return Error::Code::InvalidData;
        }

        if(const auto p = devices.find(std::string(name)); p != devices.end()) {
            return OpenInfo(p->first, *this, p->second.get(), FileType::Device, p->second->get_filesize());
        } else {
            return Error::Code::NoSuchFile;
        }
    }

    auto create(OpenInfo& info, const std::string_view name, const FileType type) -> Result<OpenInfo> override {
        return Error::Code::InvalidData;
    }

    auto readdir(OpenInfo& info, const size_t index) -> Result<OpenInfo> override {
        if(info.get_driver_data() != 0) {
            return Error::Code::InvalidData;
        }
        if(index >= devices.size()) {
            return Error::Code::EndOfFile;
        }

        const auto p = std::next(devices.begin(), index);
        return OpenInfo(p->first, *this, p->second.get(), FileType::Device, p->second->get_filesize());
    }

    auto remove(OpenInfo& info, const std::string_view name) -> Error override {
        if(info.get_driver_data() != 0) {
            return Error::Code::InvalidData;
        }

        if(const auto p = devices.find(std::string(name)); p != devices.end()) {
            devices.erase(p);
            return Success();
        } else {
            return Error::Code::NoSuchFile;
        }
    }

    auto get_device_type(OpenInfo& info) -> DeviceType override {
        auto& device = *reinterpret_cast<Device*>(info.get_driver_data());
        return device.get_device_type();
    }

    auto create_device(OpenInfo& info, const std::string_view name, const uintptr_t device_impl) -> Result<OpenInfo> override {
        if(info.get_driver_data() != 0) {
            return Error::Code::InvalidData;
        }

        if(devices.find(std::string(name)) != devices.end()) {
            return Error::Code::FileExists;
        }

        auto       device = reinterpret_cast<Device*>(device_impl);
        const auto p      = devices.emplace(name, device).first;
        return OpenInfo(p->first, *this, p->second.get(), FileType::Device, 0);
    }

    auto control_device(OpenInfo& info, const DeviceOperation op, void* const arg) -> Error override {
        if(info.get_driver_data() == 0) {
            return Error::Code::NotFile;
        }

        auto& device = *reinterpret_cast<Device*>(info.get_driver_data());
        switch(device.get_device_type()) {
        case DeviceType::None:
            return Error::Code::InvalidDeviceType;
        case DeviceType::Framebuffer: {
            auto& fb = *reinterpret_cast<FramebufferDevice*>(&device);
            switch(op) {
            case DeviceOperation::GetSize: {
                *reinterpret_cast<std::array<size_t, 2>*>(arg) = fb.get_size();
            } break;
            case DeviceOperation::GetDirectPointer: {
                *reinterpret_cast<uint8_t***>(arg) = fb.direct_access();
            } break;
            case DeviceOperation::Swap: {
                fb.swap();
            } break;
            case DeviceOperation::IsDoubleBuffered: {
                *reinterpret_cast<bool*>(arg) = fb.is_double_buffered();
            } break;
            default:
                return Error::Code::InvalidDeviceOperation;
            }
        } break;
        case DeviceType::Keyboard: {
            // auto& kb = *reinterpret_cast<KeyboardDevice*>(&device);
            switch(op) {
            default:
                return Error::Code::InvalidDeviceOperation;
            }
        } break;
        case DeviceType::Mouse: {
        } break;
        case DeviceType::Block: {
            auto& block = *reinterpret_cast<BlockDevice*>(&device);
            switch(op) {
            case DeviceOperation::GetBytesPerSector:
                *reinterpret_cast<size_t*>(arg) = block.get_bytes_per_sector();
                break;
            default:
                return Error::Code::InvalidDeviceOperation;
            }
        } break;
        }
        return Success();
    }

    auto on_handle_create(OpenInfo& info, Event& write_event) -> void override {
        if(info.get_driver_data() == 0) {
            return;
        }

        auto& device = *reinterpret_cast<Device*>(info.get_driver_data());
        device.on_handle_create(write_event);
    }

    auto on_handle_destroy(OpenInfo& info) -> void override {
        if(info.get_driver_data() == 0) {
            return;
        }

        auto& device = *reinterpret_cast<Device*>(info.get_driver_data());
        device.on_handle_destroy();
    }

    auto get_root() -> OpenInfo& override {
        return root;
    }

    Driver() : root("/", *this, nullptr, FileType::Directory, 0, OpenInfo::volume_root_attributes) {}
};

inline auto new_driver() -> Driver {
    return Driver();
}
} // namespace fs::dev
