#pragma once
#include "drivers/basic.hpp"
#include "drivers/tmp.hpp"

namespace fs {
enum class OpenMode {
    Read,
    Write,
};

inline auto follow_mountpoints(fs::OpenInfo* info) -> fs::OpenInfo* {
    while(info->mount != nullptr) {
        info = info->mount;
    }
    return info;
}

inline auto try_open(fs::OpenInfo* const info, const OpenMode mode) -> Error {
    // the file is already write opened
    if(info->write_count >= 1) {
        return Error::Code::FileOpened;
    }

    // cannot modify read opened file
    if(info->read_count >= 1 && mode != OpenMode::Read) {
        return Error::Code::FileOpened;
    }

    // cannot open exclusive file
    if(info->is_exclusive() && (info->read_count >= 1 || info->write_count >= 1)) {
        return Error::Code::FileOpened;
    }

    switch(mode) {
    case OpenMode::Read:
        info->read_count += 1;
        break;
    case OpenMode::Write:
        info->write_count += 1;
    }

    return Success();
}

class Controller;

class Handle {
    friend class Controller;

  private:
    OpenInfo* data;
    OpenMode  mode;
    Event     write_event; // data available

    auto is_write_opened() -> bool {
        if(mode != OpenMode::Write) {
            logger(LogLevel::Error, "attempt to write to ro opened file \"%s\"\n", data->name.data());
            return false;
        }
        return true;
    }

  public:
    auto read(const size_t offset, const size_t size, void* const buffer) -> Result<size_t> {
        return data->read(offset, size, buffer);
    }

    auto write(const size_t offset, const size_t size, const void* const buffer) -> Result<size_t> {
        if(!is_write_opened()) {
            return Error::Code::FileNotOpened;
        }
        return data->write(offset, size, buffer);
    }

    auto open(const std::string_view name, const OpenMode mode) -> Result<Handle> {
        auto& children     = data->children;
        auto  created_info = std::optional<OpenInfo>();
        auto  result       = (OpenInfo*)(nullptr);
        if(const auto p = children.find(std::string(name)); p != children.end()) {
            result = follow_mountpoints(&p->second);
        } else {
            auto find_result = data->find(name);
            if(!find_result) {
                return find_result.as_error();
            }
            result = &created_info.emplace(std::move(find_result.as_value()));
        }

        error_or(try_open(result, mode));

        if(created_info) {
            auto& v = created_info.value();
            result  = &children.emplace(v.name, std::move(v)).first->second;
        }

        return Handle(result, mode);
    }

    auto find(const std::string_view name) -> Result<OpenInfo> {
        return data->find(name);
    }

    auto create(const std::string_view name, const FileType type) -> Error {
        if(!is_write_opened()) {
            return Error::Code::FileNotOpened;
        }
        const auto r = data->create(name, type);
        return r ? Success() : r.as_error();
    }

    auto readdir(const size_t index) -> Result<OpenInfo> {
        return data->readdir(index);
    }

    auto remove(const std::string_view name) -> Error {
        if(!is_write_opened()) {
            return Error::Code::FileNotOpened;
        }
        return data->remove(name);
    }

    auto get_filesize() const -> size_t {
        return data->filesize;
    }

    auto get_device_type() const -> DeviceType {
        if(data->type != FileType::Device) {
            return DeviceType::None;
        }
        return data->get_device_type();
    }

    auto create_device(const std::string_view name, const uintptr_t device_impl) -> Result<OpenInfo> {
        if(!is_write_opened()) {
            return Error::Code::FileNotOpened;
        }

        return data->create_device(name, device_impl);
    }

    auto control_device(const DeviceOperation op, void* const arg) -> Error {
        return data->control_device(op, arg);
    }

    auto read_event() -> Event& {
        return write_event;
    }

    auto operator=(Handle&& o) -> Handle& {
        data        = o.data;
        mode        = o.mode;
        write_event = std::move(o.write_event);
        data->on_handle_update(write_event);
        return *this;
    }

    Handle(Handle&& o) {
        *this = std::move(o);
    }

    Handle(OpenInfo* const data, const OpenMode mode) : data(data), mode(mode) {
        data->on_handle_create(write_event);
    }

    ~Handle() {
        if(write_event.is_valid()) {
            data->on_handle_destroy();
        }
    }
};

class Controller {
  private:
    basic::Driver       basic_driver;
    OpenInfo&           root;
    std::vector<Handle> mountpoints;

    auto open_root(const OpenMode mode) -> Result<Handle> {
        auto info = follow_mountpoints(&root);
        if(const auto e = try_open(info, mode)) {
            return e;
        }
        return Handle(info, mode);
    }

    static auto split_path(const std::string_view path) -> std::vector<std::string_view> {
        auto r = std::vector<std::string_view>();

        auto end   = size_t(0);
        auto start = size_t();
        while((start = path.find_first_not_of('/', end)) != std::string_view::npos) {
            end = path.find('/', start);
            r.emplace_back(path.substr(start, end - start));
        }
        return r;
    }

    static auto find_top_mountpoint(OpenInfo* node) -> Result<OpenInfo*> {
        if(node->mount == nullptr) {
            return Error::Code::NotMounted;
        }
        while(node->mount->mount != nullptr) {
            node = node->mount;
        }
        return node;
    }

    auto open_parent_directory(std::vector<std::string_view>& elms, const OpenMode mode) -> Result<Handle> {
        if(elms.empty()) {
            return open_root(mode);
        }

        auto dirname = std::span<std::string_view>(elms.begin(), elms.size() - 1);
        auto result  = open_root(OpenMode::Read);
        if(!result) {
            return result.as_error();
        }

        for(const auto& d : dirname) {
            auto handle = std::move(result.as_value());
            result      = handle.open(d, OpenMode::Read);
            close(std::move(handle));
            if(!result) {
                return result;
            }
        }
        return result;
    }

  public:
    auto open(const std::string_view path, const OpenMode mode) -> Result<Handle> {
        auto elms = split_path(path);
        if(elms.empty()) {
            return open_root(mode);
        }

        auto filename = elms.back();
        value_or(handle, open_parent_directory(elms, mode));

        auto result = handle.open(filename, mode);
        close(std::move(handle));
        return result;
    }

    auto close(Handle handle) -> void {
        auto node = handle.data;
        switch(handle.mode) {
        case OpenMode::Read:
            node->read_count -= 1;
            break;
        case OpenMode::Write:
            node->write_count -= 1;
            break;
        }
        while(node->parent != nullptr) {
            if(node->is_busy() || node->is_volume_root()) {
                break;
            }

            if(node->parent == nullptr) {
                // bug
                // this node should be a volume root
                break;
            }

            node->parent->children.erase(node->name);
            node = node->parent;
        }
    }

    auto mount(const std::string_view path, Driver& driver) -> Error {
        auto volume_root = &driver.get_root();
        auto open_result = open(path, OpenMode::Write);
        if(!open_result) {
            return open_result.as_error();
        }
        auto handle        = std::move(open_result.as_value());
        handle.data->mount = volume_root;
        mountpoints.emplace_back(std::move(handle));
        return Success();
    }

    auto unmount(const std::string_view path) -> Result<const Driver*> {
        auto mountpoint = (OpenInfo*)(nullptr);
        auto elms       = split_path(path);
        if(elms.empty()) {
            value_or(node, find_top_mountpoint(&root));
            mountpoint = node;
        } else {
            value_or(parent, open_parent_directory(elms, OpenMode::Read));
            close(std::move(parent));
            auto& children = parent.data->children;
            if(const auto p = children.find(std::string(elms.back())); p == children.end()) {
                return Error::Code::NoSuchFile;
            } else {
                value_or(node, find_top_mountpoint(&p->second));
                mountpoint = node;
            }
        }

        for(auto m = mountpoints.begin(); m != mountpoints.end(); m += 1) {
            if(m->data != mountpoint) {
                continue;
            }

            const auto volume_root = mountpoint->mount;
            if(volume_root->is_busy() && !volume_root->is_permanent()) {
                return Error::Code::VolumeBusy;
            }
            mountpoint->mount = nullptr;

            close(std::move(*m));
            mountpoints.erase(m);
            return volume_root->read_driver();
        }
        return Error::Code::NotMounted;
    }

    Controller() : root(basic_driver.get_root()) {}
};
} // namespace fs
