#pragma once
#include <string>
#include <variant>
#include <vector>

#include "../../memory/allocator.hpp"
#include "../fs.hpp"

namespace fs::tmp {
class Object {
  private:
    std::string name;

  protected:
    Object(std::string name) : name(std::move(name)) {}

  public:
    auto get_name() const -> const std::string& {
        return name;
    }
};

class File;
class Directory;

template <class T>
concept FileObject = std::is_same_v<T, File> || std::is_same_v<T, Directory>;

class File : public Object {
  private:
    size_t                                  filesize = 0;
    std::vector<memory::SmartSingleFrameID> data;

    auto data_at(const size_t index) -> uint8_t* {
        return static_cast<uint8_t*>(data[index]->get_frame());
    }

    template <bool reverse>
    auto memory_copy(std::conditional_t<!reverse, void*, const void*> a, std::conditional_t<!reverse, const void*, void*> b, const size_t len) -> void {
        if constexpr(!reverse) {
            memcpy(a, b, len);
        } else {
            memcpy(b, a, len);
        }
    }

    template <bool write>
    auto copy(const size_t offset, size_t size, std::conditional_t<write, const uint8_t*, uint8_t*> buffer) -> Result<size_t> {
        const auto total_size = size;

        if(offset + size > filesize) {
            return Error::Code::EndOfFile;
        }

        auto frame_index = offset / memory::bytes_per_frame;

        {
            const auto offset_in_frame = offset % memory::bytes_per_frame;
            const auto size_in_frame   = memory::bytes_per_frame - offset_in_frame;
            const auto copy_len        = size < size_in_frame ? size : size_in_frame;
            memory_copy<write>(buffer, data_at(frame_index) + offset_in_frame, copy_len);
            buffer += copy_len;
            size -= copy_len;
            frame_index += 1;
        }

        while(size >= memory::bytes_per_frame) {
            memory_copy<write>(buffer, data_at(frame_index), memory::bytes_per_frame);
            buffer += memory::bytes_per_frame;
            size -= memory::bytes_per_frame;
            frame_index += 1;
        }

        if(size != 0) {
            memory_copy<write>(buffer, data_at(frame_index), size);
        }

        return size_t(total_size);
    }

  public:
    auto read(const size_t offset, const size_t size, uint8_t* const buffer) -> Result<size_t> {
        return copy<false>(offset, size, static_cast<uint8_t*>(buffer));
    }

    auto write(const size_t offset, const size_t size, const void* const buffer) -> Result<size_t> {
        return copy<true>(offset, size, static_cast<const uint8_t*>(buffer));
    }

    auto resize(const size_t new_size) -> Error {
        const auto new_data_size = (new_size + memory::bytes_per_frame - 1) / memory::bytes_per_frame;
        const auto old_data_size = data.size();
        if(new_data_size > old_data_size) {
            auto new_frames = std::vector<memory::SmartSingleFrameID>(new_data_size - old_data_size);
            for(auto& f : new_frames) {
                auto frame_r = memory::allocate_single();
                if(!frame_r) {
                    return frame_r.as_error();
                }
                auto& frame = frame_r.as_value();

                f = std::move(frame);
            }
            data.reserve(new_data_size);
            std::move(std::begin(new_frames), std::end(new_frames), std::back_inserter(data));
        } else if(new_data_size < old_data_size) {
            data.resize(new_data_size);
        }
        filesize = new_size;
        return Success();
    }

    auto get_size() const -> size_t {
        return filesize;
    }

    File(std::string name) : Object(std::move(name)) {}
};

class Directory : public Object {
  private:
    std::unordered_map<std::string, std::variant<File, Directory>> children;

  public:
    auto find(const std::string_view name) const -> const std::variant<File, Directory>* {
        // TODO
        // children.find(name)
        // https://onihusube.hatenablog.com/entry/2021/12/17/002236
        const auto p = children.find(std::string(name));
        return p != children.end() ? &p->second : nullptr;
    }

    template <FileObject T>
    auto create(const std::string_view name) -> std::variant<File, Directory>* {
        return &children.emplace(std::string(name), T(std::string(name))).first->second;
    }

    auto remove(const std::string_view name) -> bool {
        return children.erase(std::string(name)) != 0;
    }

    auto find_nth(const size_t index) const -> Result<const std::variant<File, Directory>*> {
        if(index >= children.size()) {
            return Error::Code::EndOfFile;
        }

        const auto i = std::next(children.begin(), index);
        return &i->second;
    }

    Directory(std::string name) : Object(std::move(name)) {}
};

class Driver : public fs::Driver {
  private:
    std::variant<File, Directory> data;
    FileOperator                  root;

    template <FileObject T>
    auto data_as(const uintptr_t data) -> Result<T*> {
        auto& obj = *reinterpret_cast<std::variant<File, Directory>*>(data);
        if(!std::holds_alternative<T>(obj)) {
            return std::is_same_v<T, File> ? Error::Code::NotFile : Error::Code::NotDirectory;
        }
        return &std::get<T>(obj);
    }

    auto create_file_operator(const std::variant<File, Directory>& variant) -> FileOperator {
        if(std::holds_alternative<File>(variant)) {
            auto& o = std::get<File>(variant);
            return FileOperator(o.get_name(), *this, &variant, FileType::Regular, o.get_size());
        } else {
            auto& o = std::get<Directory>(variant);
            return FileOperator(o.get_name(), *this, &variant, FileType::Directory, 0);
        }
    }

  public:
    auto read(FileOperator& fop, const size_t offset, const size_t size, void* const buffer) -> Result<size_t> override {
        auto file_r = data_as<File>(fop.get_driver_data());
        if(!file_r) {
            return file_r.as_error();
        }
        auto& file = file_r.as_value();

        return file->read(offset, size, static_cast<uint8_t*>(buffer));
    }

    auto write(FileOperator& fop, const size_t offset, const size_t size, const void* const buffer) -> Result<size_t> override {
        auto file_r = data_as<File>(fop.get_driver_data());
        if(!file_r) {
            return file_r.as_error();
        }
        auto& file = file_r.as_value();

        if(const auto e = file->resize(offset + size)) {
            return e;
        }
        return file->write(offset, size, static_cast<const uint8_t*>(buffer));
    }

    auto find(FileOperator& fop, const std::string_view name) -> Result<FileOperator> override {
        auto dir_r = data_as<Directory>(fop.get_driver_data());
        if(!dir_r) {
            return dir_r.as_error();
        }
        auto& dir = dir_r.as_value();

        const auto p = dir->find(name);
        return p != nullptr ? Result(create_file_operator(*p)) : Error::Code::NoSuchFile;
    }

    auto create(FileOperator& fop, const std::string_view name, const FileType type) -> Result<FileOperator> override {
        auto dir_r = data_as<Directory>(fop.get_driver_data());
        if(!dir_r) {
            return dir_r.as_error();
        }
        auto& dir = dir_r.as_value();

        if(dir->find(name) != nullptr) {
            return Error::Code::FileExists;
        }

        auto v = (std::variant<File, Directory>*)nullptr;
        switch(type) {
        case FileType::Regular:
            v = dir->create<File>(name);
            break;
        case FileType::Directory:
            v = dir->create<Directory>(name);
            break;
        default:
            return Error::Code::NotImplemented;
        }
        return create_file_operator(*v);
    }

    auto readdir(FileOperator& fop, const size_t index) -> Result<FileOperator> override {
        auto dir_r = data_as<Directory>(fop.get_driver_data());
        if(!dir_r) {
            return dir_r.as_error();
        }
        auto& dir = dir_r.as_value();

        auto child_r = dir->find_nth(index);
        if(!child_r) {
            return child_r.as_error();
        }
        auto& child = child_r.as_value();

        return create_file_operator(*child);
    }

    auto remove(FileOperator& fop, const std::string_view name) -> Error override {
        auto dir_r = data_as<Directory>(fop.get_driver_data());
        if(!dir_r) {
            return dir_r.as_error();
        }
        auto& dir = dir_r.as_value();

        if(!dir->remove(name)) {
            return Error::Code::NoSuchFile;
        }
        return Success();
    }

    auto get_root() -> FileOperator& override {
        return root;
    }

    Driver() : data(Directory("/")),
               root("/", *this, &data, FileType::Directory, 0, FileOperator::volume_root_attributes) {}
};
} // namespace fs::tmp
