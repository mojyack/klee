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
        return Error::Code::InvalidData;
    }

    auto create(const DriverData data, const std::string_view name, const FileType type) -> Result<OpenInfo> override {
        return Error::Code::InvalidData;
    }

    auto readdir(const DriverData data, const size_t index) -> Result<OpenInfo> override {
        return Error::Code::InvalidData;
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
