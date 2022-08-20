#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../error.hpp"
#include "../log.hpp"
#include "../mutex.hpp"

namespace fs {
enum class FileType : uint32_t {
    Regular,
    Directory,
    Device,
};

enum class DeviceType : uint32_t {
    None,
    Framebuffer,
    Keyboard,
    Mouse,
    Block,
};

enum class DeviceOperation {
    // Framebuffer
    GetSize,
    GetDirectPointer,
    Swap,
    IsDoubleBuffered,
    // Block
    GetBytesPerSector,
};

class Driver;

class OpenInfo {
  private:
    Driver*   driver;
    uintptr_t driver_data;
    bool      volume_root;
    bool      exclusive;
    bool      permanent; // even if the volume is unmounted, this OpenInfo is not deleted

    auto check_opened(const bool write) -> bool {
        if((write && write_count == 0) || (!write && read_count == 0 && write_count == 0)) {
            logger(LogLevel::Error, "file \"%s\" is not %s opened\n", name.data(), write ? "write" : "read");
            return false;
        }
        return true;
    }

  public:
    std::string name;
    uint32_t    read_count  = 0;
    uint32_t    write_count = 0;
    FileType    type;
    size_t      filesize;
    OpenInfo*   parent = nullptr;
    OpenInfo*   mount  = nullptr;

    std::unordered_map<std::string, OpenInfo> children;

    auto read(size_t offset, size_t size, void* buffer) -> Result<size_t>;
    auto write(size_t offset, size_t size, const void* buffer) -> Result<size_t>;
    auto find(std::string_view name) -> Result<OpenInfo>;
    auto create(std::string_view name, FileType type) -> Result<OpenInfo>;
    auto readdir(size_t index) -> Result<OpenInfo>;
    auto remove(std::string_view name) -> Error;
    auto get_device_type() -> DeviceType; // can be used without opening
    auto create_device(std::string_view name, uintptr_t device_impl) -> Result<OpenInfo>;
    auto control_device(DeviceOperation op, void* arg) -> Error;

    // used by Handle
    auto on_handle_create(Event& write_event) -> void;
    auto on_handle_destroy() -> void;

    // used by Controller
    auto is_busy() const -> bool {
        return read_count != 0 || write_count != 0 || !children.empty() || mount != nullptr;
    }

    auto is_volume_root() const -> bool {
        return volume_root;
    }

    auto is_exclusive() const -> bool {
        return exclusive;
    }

    auto is_permanent() const -> bool {
        return permanent;
    }

    auto read_driver() const -> const Driver* {
        return driver;
    }

    // used by Driver
    auto get_driver_data() const -> uintptr_t {
        return driver_data;
    }

    OpenInfo(const std::string_view name,
             Driver&                driver,
             const auto             driver_data,
             const FileType         type,
             const size_t           filesize,
             const bool             volume_root = false,
             const bool             exclusive   = false,
             const bool             permanent   = false)
        : driver(&driver),
          driver_data((uintptr_t)driver_data),
          volume_root(volume_root),
          exclusive(exclusive),
          permanent(permanent),
          name(name),
          type(type),
          filesize(filesize) {}

    // test stuff
    struct Testdata {
        std::string                               name;
        uint32_t                                  read_count  = 0;
        uint32_t                                  write_count = 0;
        FileType                                  type;
        std::shared_ptr<Testdata>                 mount;
        std::unordered_map<std::string, Testdata> children;
    };

    auto test_compare(const Testdata& data) const -> bool {
        if(name != data.name || read_count != data.read_count || write_count != data.write_count || type != data.type) {
            return false;
        }
        if(children.size() != data.children.size()) {
            return false;
        }
        for(auto& [k, v] : children) {
            if(auto p = data.children.find(k); p == data.children.end()) {
                return false;
            } else {
                v.test_compare(p->second);
            }
        }
        auto i1 = children.begin();
        auto i2 = data.children.begin();
        while(i1 != children.end()) {
            if(!i1->second.test_compare(i2->second)) {
                return false;
            }
            i1 = std::next(i1, 1);
            i2 = std::next(i2, 1);
        }

        if(mount != nullptr) {
            if(!data.mount) {
                return false;
            }
            return mount->test_compare(*data.mount.get());
        }

        return true;
    }
};

class Driver {
  public:
    virtual auto read(OpenInfo& info, size_t offset, size_t size, void* buffer) -> Result<size_t>        = 0;
    virtual auto write(OpenInfo& info, size_t offset, size_t size, const void* buffer) -> Result<size_t> = 0;

    virtual auto find(OpenInfo& info, std::string_view name) -> Result<OpenInfo>                  = 0;
    virtual auto create(OpenInfo& info, std::string_view name, FileType type) -> Result<OpenInfo> = 0;
    virtual auto readdir(OpenInfo& info, size_t index) -> Result<OpenInfo>                        = 0;
    virtual auto remove(OpenInfo& info, std::string_view name) -> Error                           = 0;

    virtual auto get_device_type(OpenInfo& info) -> DeviceType {
        return DeviceType::None;
    }

    virtual auto create_device(OpenInfo& info, std::string_view name, uintptr_t device_impl) -> Result<OpenInfo> {
        return Error::Code::NotImplemented;
    }

    virtual auto control_device(OpenInfo& info, DeviceOperation op, void* arg) -> Error {
        return Error::Code::NotImplemented;
    }

    virtual auto on_handle_create(OpenInfo& info, Event& write_event) -> void {}
    virtual auto on_handle_destroy(OpenInfo& info) -> void {}

    virtual auto get_root() -> OpenInfo& = 0;

    virtual ~Driver() = default;
};

inline auto OpenInfo::read(const size_t offset, const size_t size, void* const buffer) -> Result<size_t> {
    if(!check_opened(false)) {
        return Error::Code::FileNotOpened;
    }
    return driver->read(*this, offset, size, buffer);
}

inline auto OpenInfo::write(const size_t offset, const size_t size, const void* const buffer) -> Result<size_t> {
    if(!check_opened(true)) {
        return Error::Code::FileNotOpened;
    }
    return driver->write(*this, offset, size, buffer);
}

inline auto OpenInfo::find(const std::string_view name) -> Result<OpenInfo> {
    if(!check_opened(false)) {
        return Error::Code::FileNotOpened;
    }

    auto r = driver->find(*this, name);
    if(r) {
        r.as_value().parent = this;
    }
    return r;
}

inline auto OpenInfo::create(const std::string_view name, const FileType type) -> Result<OpenInfo> {
    if(!check_opened(true)) {
        return Error::Code::FileNotOpened;
    }
    return driver->create(*this, name, type);
}

inline auto OpenInfo::readdir(const size_t index) -> Result<OpenInfo> {
    if(!check_opened(false)) {
        return Error::Code::FileNotOpened;
    }

    auto r = driver->readdir(*this, index);
    if(r) {
        r.as_value().parent = this;
    }
    return r;
}

inline auto OpenInfo::remove(const std::string_view name) -> Error {
    if(!check_opened(true)) {
        return Error::Code::FileNotOpened;
    }
    if(children.find(std::string(name)) != children.end()) {
        return Error::Code::FileOpened;
    }
    return driver->remove(*this, name);
}

inline auto OpenInfo::get_device_type() -> DeviceType {
    if(type != FileType::Device) {
        return DeviceType::None;
    }

    return driver->get_device_type(*this);
}

inline auto OpenInfo::create_device(const std::string_view name, const uintptr_t device_impl) -> Result<OpenInfo> {
    if(!check_opened(true)) {
        return Error::Code::FileNotOpened;
    }

    return driver->create_device(*this, name, device_impl);
}

inline auto OpenInfo::control_device(const DeviceOperation op, void* const arg) -> Error {
    // TODO
    // is this control requires write access?
    if(!check_opened(false)) {
        return Error::Code::FileNotOpened;
    }

    return driver->control_device(*this, op, arg);
}

inline auto OpenInfo::on_handle_create(Event& write_event) -> void {
    driver->on_handle_create(*this, write_event);
}

inline auto OpenInfo::on_handle_destroy() -> void {
    driver->on_handle_destroy(*this);
}
} // namespace fs
