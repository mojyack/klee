#pragma once
#include "../fs.hpp"

namespace fs::basic {
class Driver : public fs::Driver {
  private:
    OpenInfo root;

  public:
    auto read(OpenInfo& info, size_t offset, size_t size, void* buffer) -> Result<size_t> override {
        return Error::Code::InvalidData;
    }

    auto write(OpenInfo& info, size_t offset, size_t size, const void* buffer) -> Result<size_t> override {
        return Error::Code::InvalidData;
    }

    auto find(OpenInfo& info, const std::string_view name) -> Result<OpenInfo> override {
        if(info.get_driver_data() != 0) {
            return Error::Code::InvalidData;
        }
        if(name != "dev") {
            return Error::Code::NoSuchFile;
        }
        return OpenInfo("dev", *this, 1, FileType::Directory, 0);
    }

    auto create(OpenInfo& info, const std::string_view name, const FileType type) -> Result<OpenInfo> override {
        return Error::Code::InvalidData;
    }

    auto readdir(OpenInfo& info, const size_t index) -> Result<OpenInfo> override {
        if(info.get_driver_data() != 0) {
            return Error::Code::InvalidData;
        }
        if(index != 0) {
            return Error::Code::EndOfFile;
        }
        return OpenInfo("dev", *this, 1, FileType::Directory, 0);
    }

    auto remove(OpenInfo& info, const std::string_view name) -> Error override {
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
