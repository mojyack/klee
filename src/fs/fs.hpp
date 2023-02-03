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

class OpenInfo;

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

    virtual auto create_device(OpenInfo& info, std::string_view name, uintptr_t device_impl) -> Result<OpenInfo>;

    virtual auto control_device(OpenInfo& info, DeviceOperation op, void* arg) -> Error {
        return Error::Code::NotImplemented;
    }

    virtual auto on_handle_create(OpenInfo& info, Event& write_event) -> void {}
    virtual auto on_handle_destroy(OpenInfo& info) -> void {}

    virtual auto get_root() -> OpenInfo& = 0;

    virtual ~Driver() = default;
};

enum class OpenLevel : int {
    Block  = 0,
    Single = 1,
    Multi  = 2,
};

class OpenInfo {
  public:
    struct Attributes {
        OpenLevel read_level : 2;
        OpenLevel write_level : 2;
        bool      exclusive;
        bool      volume_root;
    };

    // attribute templates
    static constexpr auto default_attributes = Attributes{
        .read_level  = OpenLevel::Single,
        .write_level = OpenLevel::Single,
        .exclusive   = true,
        .volume_root = false};

    static constexpr auto volume_root_attributes = Attributes{
        .read_level  = OpenLevel::Single,
        .write_level = OpenLevel::Single,
        .exclusive   = true,
        .volume_root = true};

  private:
    Driver* const driver;
    uintptr_t     driver_data;

    auto assert_opened(const bool write) -> bool {
        if((write && write_count == 0) || (!write && read_count == 0 && write_count == 0)) {
            // kernel bug
            logger(LogLevel::Error, "kernel: file \"%s\" is not %s opened\n", name.data(), write ? "write" : "read");
            return false;
        }
        return true;
    }

  public:
    uint32_t          read_count  = 0;
    uint32_t          write_count = 0;
    size_t            filesize;
    OpenInfo*         parent = nullptr;
    OpenInfo*         mount  = nullptr;
    const std::string name;
    const FileType    type;
    const Attributes  attributes;

    std::unordered_map<std::string, OpenInfo> children;

    auto read(const size_t offset, const size_t size, void* const buffer) -> Result<size_t> {
        if(!assert_opened(false)) {
            return Error::Code::FileNotOpened;
        }
        return driver->read(*this, offset, size, buffer);
    }

    auto write(const size_t offset, const size_t size, const void* const buffer) -> Result<size_t> {
        if(!assert_opened(true)) {
            return Error::Code::FileNotOpened;
        }
        return driver->write(*this, offset, size, buffer);
    }

    auto find(const std::string_view name) -> Result<OpenInfo> {
        if(!assert_opened(false)) {
            return Error::Code::FileNotOpened;
        }

        auto r = driver->find(*this, name);
        if(r) {
            r.as_value().parent = this;
        }
        return r;
    }

    auto create(const std::string_view name, const FileType type) -> Result<OpenInfo> {
        if(!assert_opened(true)) {
            return Error::Code::FileNotOpened;
        }
        return driver->create(*this, name, type);
    }

    auto readdir(const size_t index) -> Result<OpenInfo> {
        if(!assert_opened(false)) {
            return Error::Code::FileNotOpened;
        }

        auto r = driver->readdir(*this, index);
        if(r) {
            r.as_value().parent = this;
        }
        return r;
    }

    auto remove(const std::string_view name) -> Error {
        if(!assert_opened(true)) {
            return Error::Code::FileNotOpened;
        }
        if(children.find(std::string(name)) != children.end()) {
            return Error::Code::FileOpened;
        }
        return driver->remove(*this, name);
    }

    auto get_device_type() -> DeviceType { // can be used without opening
        if(type != FileType::Device) {
            return DeviceType::None;
        }

        return driver->get_device_type(*this);
    }

    auto create_device(const std::string_view name, const uintptr_t device_impl) -> Result<OpenInfo> {
        if(!assert_opened(true)) {
            return Error::Code::FileNotOpened;
        }

        return driver->create_device(*this, name, device_impl);
    }

    auto control_device(const DeviceOperation op, void* const arg) -> Error {
        // TODO
        // is this control requires write access?
        return driver->control_device(*this, op, arg);
    }

    // used by Handle
    auto on_handle_create(Event& write_event) -> void {
        driver->on_handle_create(*this, write_event);
    }

    auto on_handle_destroy() -> void {
        driver->on_handle_destroy(*this);
    }

    // used by Controller
    auto is_busy() const -> bool {
        return read_count != 0 || write_count != 0 || !children.empty() || mount != nullptr;
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
             const Attributes       attributes = default_attributes)
        : driver(&driver),
          driver_data((uintptr_t)driver_data),
          filesize(filesize),
          name(name),
          type(type),
          attributes(attributes) {}
};

inline auto Driver::create_device(OpenInfo& info, std::string_view name, uintptr_t device_impl) -> Result<OpenInfo> {
    return Error::Code::NotImplemented;
}
} // namespace fs
