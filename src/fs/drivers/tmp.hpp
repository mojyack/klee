#pragma once
#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "../../macro.hpp"
#include "../../memory-manager.hpp"
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
    size_t                    filesize = 0;
    std::vector<SmartFrameID> data;

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
    auto copy(const size_t offset, size_t size, std::conditional_t<write, const uint8_t*, uint8_t*> buffer) -> Error {
        if(offset + size > filesize) {
            return Error::Code::EndOfFile;
        }

        auto frame_index = offset / bytes_per_frame;

        {
            const auto offset_in_frame = offset % bytes_per_frame;
            const auto size_in_frame   = bytes_per_frame - offset_in_frame;
            const auto copy_len        = size < size_in_frame ? size : size_in_frame;
            memory_copy<write>(buffer, data_at(frame_index) + offset_in_frame, copy_len);
            buffer += copy_len;
            size -= copy_len;
            frame_index += 1;
        }

        while(size >= bytes_per_frame) {
            memory_copy<write>(buffer, data_at(frame_index), bytes_per_frame);
            buffer += bytes_per_frame;
            size -= bytes_per_frame;
            frame_index += 1;
        }

        if(size != 0) {
            memory_copy<write>(buffer, data_at(frame_index), size);
        }

        return Error();
    }

  public:
    auto read(const size_t offset, const size_t size, uint8_t* const buffer) -> Error {
        return copy<false>(offset, size, static_cast<uint8_t*>(buffer));
    }

    auto write(const size_t offset, const size_t size, const void* const buffer) -> Error {
        return copy<true>(offset, size, static_cast<const uint8_t*>(buffer));
    }

    auto resize(const size_t new_size) -> Error {
        const auto new_data_size = (new_size + bytes_per_frame - 1) / bytes_per_frame;
        const auto old_data_size = data.size();
        if(new_data_size > old_data_size) {
            auto new_frames = std::vector<SmartFrameID>(new_data_size - old_data_size);
            for(auto& f : new_frames) {
                auto frame = allocator->allocate(1);
                if(!frame) {
                    return frame.as_error();
                }
                f = SmartFrameID(frame.as_value(), 1);
            }
            data.reserve(new_data_size);
            std::move(std::begin(new_frames), std::end(new_frames), std::back_inserter(data));
        } else if(new_data_size < old_data_size) {
            data.resize(new_data_size);
        }
        filesize = new_size;
        return Error();
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
            return Error::Code::IndexOutOfRange;
        }

        const auto i = std::next(children.begin(), index);
        return &i->second;
    }

    Directory(std::string name) : Object(std::move(name)) {}
};

class Driver : public fs::Driver {
  private:
    std::variant<File, Directory> data;
    OpenInfo                      root;

    template <FileObject T>
    auto data_as(const DriverData& data) -> Result<T*> {
        auto& obj = *reinterpret_cast<std::variant<File, Directory>*>(data.num);
        if(!std::holds_alternative<T>(obj)) {
            return std::is_same_v<T, File> ? Error::Code::NotFile : Error::Code::NotDirectory;
        }
        return &std::get<T>(obj);
    }

    auto create_openinfo(const std::variant<File, Directory>& variant) -> OpenInfo {
        if(std::holds_alternative<File>(variant)) {
            auto& o = std::get<File>(variant);
            return OpenInfo(o.get_name(), *this, &variant, FileType::Regular, o.get_size());
        } else {
            auto& o = std::get<Directory>(variant);
            return OpenInfo(o.get_name(), *this, &variant, FileType::Directory, 0);
        }
    }

  public:
    auto read(const DriverData data, const size_t offset, const size_t size, void* const buffer) -> Error override {
        value_or(file, data_as<File>(data));
        return file->read(offset, size, static_cast<uint8_t*>(buffer));
    }

    auto write(const DriverData data, const size_t offset, const size_t size, const void* const buffer) -> Error override {
        value_or(file, data_as<File>(data));
        file->resize(offset + size);
        return file->write(offset, size, static_cast<const uint8_t*>(buffer));
    }

    auto find(const DriverData data, const std::string_view name) -> Result<OpenInfo> override {
        value_or(dir, data_as<Directory>(data));
        const auto p = dir->find(name);
        return p != nullptr ? Result(create_openinfo(*p)) : Error::Code::NoSuchFile;
    }

    auto create(const DriverData data, const std::string_view name, const FileType type) -> Result<OpenInfo> override {
        value_or(dir, data_as<Directory>(data));
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
        return create_openinfo(*v);
    }

    auto readdir(const DriverData data, const size_t index) -> Result<OpenInfo> override {
        value_or(dir, data_as<Directory>(data));
        value_or(child, dir->find_nth(index));
        return create_openinfo(*child);
    }

    auto remove(const DriverData data, const std::string_view name) -> Error override {
        value_or(dir, data_as<Directory>(data));
        if(!dir->remove(name)) {
            return Error::Code::NoSuchFile;
        }
        return Error();
    }

    auto get_root() -> OpenInfo& override {
        return root;
    }

    Driver() : data(Directory("/")),
               root("/", *this, &data, FileType::Directory, 0, true) {}
};

inline auto new_driver() -> Driver {
    return Driver();
}
} // namespace fs::tmp
