#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../error.hpp"
#include "../log.hpp"

namespace fs {
enum class FileType : uint32_t {
    Regular,
    Directory,
};

class Driver;

class OpenInfo {
  private:
    Driver*   driver;
    uintptr_t driver_data;
    bool      volume_root;

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
    uint32_t    child_count = 0;
    FileType    type;
    size_t      size;
    OpenInfo*   parent = nullptr;
    OpenInfo*   mount  = nullptr;

    std::unordered_map<std::string, OpenInfo> children;

    auto read(size_t offset, size_t size, void* buffer) -> Error;
    auto write(size_t offset, size_t size, const void* buffer) -> Error;
    auto find(std::string_view name) -> Result<OpenInfo>;
    auto create(std::string_view name, FileType type) -> Result<OpenInfo>;
    auto readdir(size_t index) -> Result<OpenInfo>;
    auto remove(std::string_view name) -> Error;

    auto is_busy() const -> bool {
        return read_count != 0 || write_count != 0 || child_count != 0 || mount != nullptr;
    }

    auto is_volume_root() const -> bool {
        return volume_root;
    }

    OpenInfo(const std::string_view name, Driver& driver, const auto driver_data, const FileType type, const size_t size, const bool volume_root = false) : driver(&driver),
                                                                                                                                                            driver_data((uintptr_t)driver_data),
                                                                                                                                                            volume_root(volume_root),
                                                                                                                                                            name(name),
                                                                                                                                                            type(type),
                                                                                                                                                            size(size) {}

    // test stuff
    struct Testdata {
        std::string                               name;
        uint32_t                                  read_count  = 0;
        uint32_t                                  write_count = 0;
        uint32_t                                  child_count = 0;
        FileType                                  type;
        std::shared_ptr<Testdata>                 mount;
        std::unordered_map<std::string, Testdata> children;
    };

    auto test_compare(const Testdata& data) const -> bool {
        if(name != data.name || read_count != data.read_count || write_count != data.write_count || child_count != data.child_count || type != data.type) {
            return false;
        }
        if(children.size() != data.children.size()) {
            return false;
        }
        for(auto [k, v] : children) {
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

struct DriverData {
    FileType  type;
    size_t    size;
    uintptr_t num;
};

class Driver {
  public:
    virtual auto read(DriverData data, size_t offset, size_t size, void* buffer) -> Error        = 0;
    virtual auto write(DriverData data, size_t offset, size_t size, const void* buffer) -> Error = 0;

    virtual auto find(DriverData data, std::string_view name) -> Result<OpenInfo>                  = 0;
    virtual auto create(DriverData data, std::string_view name, FileType type) -> Result<OpenInfo> = 0;
    virtual auto readdir(DriverData data, size_t index) -> Result<OpenInfo>                        = 0;
    virtual auto remove(DriverData data, std::string_view name) -> Error                           = 0;

    virtual auto get_root() -> OpenInfo& = 0;

    virtual ~Driver() = default;
};

inline auto OpenInfo::read(const size_t offset, const size_t size, void* const buffer) -> Error {
    if(!check_opened(false)) {
        return Error::Code::FileNotOpened;
    }
    return driver->read({type, size, driver_data}, offset, size, buffer);
}

inline auto OpenInfo::write(const size_t offset, const size_t size, const void* const buffer) -> Error {
    if(!check_opened(true)) {
        return Error::Code::FileNotOpened;
    }
    return driver->write({type, size, driver_data}, offset, size, buffer);
}

inline auto OpenInfo::find(const std::string_view name) -> Result<OpenInfo> {
    if(!check_opened(false)) {
        return Error::Code::FileNotOpened;
    }

    auto r = driver->find({type, size, driver_data}, name);
    if(r) {
        r.as_value().parent = this;
    }
    return r;
}

inline auto OpenInfo::create(const std::string_view name, const FileType type) -> Result<OpenInfo> {
    if(!check_opened(true)) {
        return Error::Code::FileNotOpened;
    }
    return driver->create({type, size, driver_data}, name, type);
}

inline auto OpenInfo::readdir(const size_t index) -> Result<OpenInfo> {
    if(!check_opened(false)) {
        return Error::Code::FileNotOpened;
    }

    auto r = driver->readdir({type, size, driver_data}, index);
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
    return driver->remove({type, size, driver_data}, name);
}
} // namespace fs
