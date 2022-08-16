#pragma once
#include "../fs.hpp"

namespace fs::basic {
class Driver : public fs::Driver {
  private:
    OpenInfo root;

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
        if(name != "dev") {
            return Error::Code::NoSuchFile;
        }
        return OpenInfo("dev", *this, 1, FileType::Directory, 0);
    }

    auto create(const DriverData data, const std::string_view name, const FileType type) -> Result<OpenInfo> override {
        return Error::Code::InvalidData;
    }

    auto readdir(const DriverData data, const size_t index) -> Result<OpenInfo> override {
        if(data.num != 0) {
            return Error::Code::InvalidData;
        }
        if(index != 0) {
            return Error::Code::EndOfFile;
        }
        return OpenInfo("dev", *this, 1, FileType::Directory, 0);
    }

    auto remove(const DriverData data, const std::string_view name) -> Error override {
        return Error::Code::InvalidData;
    }

    auto get_root() -> OpenInfo& override {
        return root;
    }

    Driver() : root("/", *this, nullptr, FileType::Directory, 0, true) {}
};

inline auto new_driver() -> Driver {
    return Driver();
}
} // namespace fs::basic
