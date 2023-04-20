#pragma once
#include <string>
#include <variant>
#include <vector>

#include "../../memory/allocator.hpp"
#include "../../util/string-map.hpp"
#include "../driver.hpp"

namespace fs::tmp {
struct File;
struct Directory;

template <class T>
concept FileObject = std::is_same_v<T, File> || std::is_same_v<T, Directory>;

struct File {
    std::string name;
    size_t      filesize = 0;
};

struct Directory {
    std::string                              name;
    StringMap<std::variant<File, Directory>> children;
};

class Driver : public fs::Driver {
  private:
    std::variant<File, Directory> data;
    FileAbstractWithDriverData    root;

    template <FileObject T>
    auto data_as(const uint64_t data) -> Result<T*> {
        auto& obj = *std::bit_cast<std::variant<File, Directory>*>(data);
        if(!std::holds_alternative<T>(obj)) {
            return std::is_same_v<T, File> ? Error::Code::NotFile : Error::Code::NotDirectory;
        }
        return &std::get<T>(obj);
    }

    auto is_file(const uint64_t fop_data) -> bool {
        auto& obj = *std::bit_cast<std::variant<File, Directory>*>(fop_data);
        return std::holds_alternative<File>(obj);
    }

    auto build_abstract(const std::variant<File, Directory>& variant) -> FileAbstractWithDriverData {
        const auto file       = std::holds_alternative<File>(variant);
        const auto attributes = fs::Attributes{
            .read_level    = OpenLevel::Single,
            .write_level   = OpenLevel::Single,
            .exclusive     = true,
            .volume_root   = false,
            .cache         = true,
            .keep_on_close = file,
        };

        if(file) {
            auto& o = std::get<File>(variant);
            return FileAbstractWithDriverData{{o.name, o.filesize, FileType::Regular, 0, attributes}, std::bit_cast<uint64_t>(&variant)};
        } else {
            auto& o = std::get<Directory>(variant);
            return FileAbstractWithDriverData{{o.name, o.children.size(), FileType::Directory, 0, attributes}, std::bit_cast<uint64_t>(&variant)};
        }
    }

  public:
    auto read(const uint64_t fop_data, uint64_t& handle_data, const size_t block, const size_t count, void* const buffer) -> Result<size_t> override {
        return is_file(fop_data) ? Error::Code::Success : Error::Code::NotFile;
    }

    auto write(const uint64_t fop_data, uint64_t& handle_data, const size_t block, const size_t count, const void* const buffer) -> Result<size_t> override {
        return is_file(fop_data) ? Error::Code::Success : Error::Code::NotFile;
    }

    auto find(const uint64_t fop_data, uint64_t& handle_data, const std::string_view name) -> Result<FileAbstractWithDriverData> override {
        auto dir_r = data_as<Directory>(fop_data);
        if(!dir_r) {
            return dir_r.as_error();
        }
        auto& dir = dir_r.as_value();

        if(const auto p = dir->children.find(name); p != dir->children.end()) {
            return build_abstract(p->second);
        } else {
            return Error::Code::NoSuchFile;
        }
    }

    auto create(const uint64_t fop_data, uint64_t& handle_data, const std::string_view name, const FileType type) -> Result<FileAbstractWithDriverData> override {
        auto dir_r = data_as<Directory>(fop_data);
        if(!dir_r) {
            return dir_r.as_error();
        }
        auto& dir = dir_r.as_value();

        if(dir->children.find(name) != dir->children.end()) {
            return Error::Code::FileExists;
        }

        auto v = (std::variant<File, Directory>*)(nullptr);
        switch(type) {
        case FileType::Regular:
            v = &dir->children.emplace(name, File{std::string(name), 0}).first->second;
            break;
        case FileType::Directory:
            v = &dir->children.emplace(name, Directory{std::string(name), {}}).first->second;
            break;
        default:
            return Error::Code::NotImplemented;
        }
        return build_abstract(*v);
    }

    auto readdir(const uint64_t fop_data, uint64_t& handle_data, const size_t index) -> Result<FileAbstractWithDriverData> override {
        auto dir_r = data_as<Directory>(fop_data);
        if(!dir_r) {
            return dir_r.as_error();
        }
        auto& dir = dir_r.as_value();

        if(index == dir->children.size()) {
            return Error::Code::EndOfFile;
        } else if(index > dir->children.size()) {
            return Error::Code::IndexOutOfRange;
        }

        return build_abstract(std::next(dir->children.begin(), index)->second);
    }

    auto remove(const uint64_t fop_data, uint64_t& handle_data, const std::string_view name) -> Error override {
        auto dir_r = data_as<Directory>(fop_data);
        if(!dir_r) {
            return dir_r.as_error();
        }
        auto& dir = dir_r.as_value();

        if(const auto p = dir->children.find(name); p == dir->children.end()) {
            return Error::Code::NoSuchFile;
        } else {
            dir->children.erase(p);
            return Success();
        }
    }

    auto get_root() -> FileAbstractWithDriverData& override {
        return root;
    }

    Driver() : data(Directory{"/", {}}),
               root{{"/", 0, FileType::Directory, 0, fs::volume_root_attributes}, std::bit_cast<uint64_t>(&data)} {
    }
};
} // namespace fs::tmp
