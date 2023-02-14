#pragma once
#include "../fs.hpp"

namespace fs::basic {
class Driver : public fs::Driver {
  private:
    FileOperator root;

  public:
    auto read(FileOperator& fop, size_t offset, size_t size, void* buffer) -> Result<size_t> override {
        return Error::Code::InvalidData;
    }

    auto write(FileOperator& fop, size_t offset, size_t size, const void* buffer) -> Result<size_t> override {
        return Error::Code::InvalidData;
    }

    auto find(FileOperator& fop, const std::string_view name) -> Result<FileOperator> override {
        if(fop.get_driver_data() != 0) {
            return Error::Code::InvalidData;
        }
        if(name != "dev") {
            return Error::Code::NoSuchFile;
        }
        return FileOperator("dev", *this, 1, FileType::Directory, 0);
    }

    auto create(FileOperator& fop, const std::string_view name, const FileType type) -> Result<FileOperator> override {
        return Error::Code::InvalidData;
    }

    auto readdir(FileOperator& fop, const size_t index) -> Result<FileOperator> override {
        if(fop.get_driver_data() != 0) {
            return Error::Code::InvalidData;
        }
        if(index != 0) {
            return Error::Code::EndOfFile;
        }
        return FileOperator("dev", *this, 1, FileType::Directory, 0);
    }

    auto remove(FileOperator& fop, const std::string_view name) -> Error override {
        return Error::Code::InvalidData;
    }

    auto get_root() -> FileOperator& override {
        return root;
    }

    Driver() : root("/", *this, nullptr, FileType::Directory, 0, FileOperator::volume_root_attributes) {}
};

inline auto new_driver() -> Driver {
    return Driver();
}
} // namespace fs::basic
