#pragma once
#include "../driver.hpp"

namespace fs::basic {
class Driver : public fs::Driver {
  private:
    FileAbstractWithDriverData root;

  public:
    auto find(const uint64_t fop_data, uint64_t& handle_data, const std::string_view name) -> Result<FileAbstractWithDriverData> override {
        if(fop_data != 0) {
            return Error::Code::InvalidData;
        }
        if(name != "dev") {
            return Error::Code::NoSuchFile;
        }

        return FileAbstractWithDriverData{{"dev", 0, FileType::Directory, 0, fs::default_attributes}, 1};
    }

    auto create(const uint64_t fop_data, uint64_t& handle_data, const std::string_view name, const FileType type) -> Result<FileAbstractWithDriverData> override {
        return Error::Code::NotSupported;
    }

    auto readdir(const uint64_t fop_data, uint64_t& handle_data, const size_t index) -> Result<FileAbstractWithDriverData> override {
        if(fop_data != 0) {
            return Error::Code::InvalidData;
        }
        if(index != 0) {
            return Error::Code::EndOfFile;
        }

        return FileAbstractWithDriverData{{"dev", 0, FileType::Directory, 0, fs::default_attributes}, 1};
    }

    auto remove(const uint64_t fop_data, uint64_t& handle_data, const std::string_view name) -> Error override {
        return Error::Code::NotSupported;
    }

    auto get_root() -> FileAbstractWithDriverData& override {
        return root;
    }

    Driver() : root{{"/", 1, FileType::Directory, 0, fs::volume_root_attributes}, 0} {
    }
};

inline auto new_driver() -> Driver {
    return Driver();
}
} // namespace fs::basic
