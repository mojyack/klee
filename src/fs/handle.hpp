#pragma once
#include "fs.hpp"

namespace fs {
struct OpenMode {
    bool read;
    bool write;
};

constexpr auto open_ro = OpenMode{.read = true, .write = false};
constexpr auto open_wo = OpenMode{.read = false, .write = true};
constexpr auto open_rw = OpenMode{.read = true, .write = true};

inline auto follow_mountpoints(fs::OpenInfo* info) -> fs::OpenInfo* {
    while(info->mount != nullptr) {
        info = info->mount;
    }
    return info;
}

inline auto try_open(fs::OpenInfo* const info, const OpenMode mode) -> Error {
    if(mode.read) {
        switch(info->attributes.read_level) {
        case OpenLevel::Block:
            return Error::Code::InvalidOpenMode;
        case OpenLevel::Single:
            if(info->read_count != 0) {
                return Error::Code::FileOpened;
            }
            [[fallthrough]];
        case OpenLevel::Multi:
            if(info->attributes.exclusive && info->write_count != 0) {
                return Error::Code::FileOpened;
            }
            break;
        }
    }
    if(mode.write) {
        switch(info->attributes.write_level) {
        case OpenLevel::Block:
            return Error::Code::InvalidOpenMode;
        case OpenLevel::Single:
            if(info->write_count != 0) {
                return Error::Code::FileOpened;
            }
            [[fallthrough]];
        case OpenLevel::Multi:
            if(info->attributes.exclusive && info->read_count != 0) {
                return Error::Code::FileOpened;
            }
            break;
        }
    }

    if(mode.read) {
        info->read_count += 1;
    }
    if(mode.write) {
        info->write_count += 1;
    }

    return Success();
}

class Handle {
    friend class Manager;

  private:
    OpenInfo*              data = nullptr;
    OpenMode               mode;
    std::unique_ptr<Event> write_event; // data available

  public:
    auto read(const size_t offset, const size_t size, void* const buffer) -> Result<size_t> {
        if(!mode.read) {
            return Error::Code::FileNotOpened;
        }

        return data->read(offset, size, buffer);
    }

    auto write(const size_t offset, const size_t size, const void* const buffer) -> Result<size_t> {
        if(!mode.write) {
            return Error::Code::FileNotOpened;
        }

        return data->write(offset, size, buffer);
    }

    auto open(const std::string_view name, const OpenMode open_mode) -> Result<Handle> {
        if(!mode.read) {
            return Error::Code::FileNotOpened;
        }

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

        if(const auto e = try_open(result, open_mode)) {
            return e;
        }

        if(created_info) {
            auto& v = created_info.value();
            result  = &children.emplace(v.name, std::move(v)).first->second;
        }

        return Handle(result, open_mode);
    }

    auto find(const std::string_view name) -> Result<OpenInfo> {
        if(!mode.read) {
            return Error::Code::FileNotOpened;
        }

        return data->find(name);
    }

    auto create(const std::string_view name, const FileType type) -> Error {
        if(!mode.write) {
            return Error::Code::FileNotOpened;
        }

        const auto r = data->create(name, type);
        return r ? Success() : r.as_error();
    }

    auto readdir(const size_t index) -> Result<OpenInfo> {
        if(!mode.read) {
            return Error::Code::FileNotOpened;
        }

        return data->readdir(index);
    }

    auto remove(const std::string_view name) -> Error {
        if(!mode.write) {
            return Error::Code::FileNotOpened;
        }

        return data->remove(name);
    }

    auto close() -> void;

    auto get_filesize() const -> Result<size_t> {
        if(!mode.read) {
            return Error::Code::FileNotOpened;
        }

        return data->filesize;
    }

    auto get_device_type() const -> Result<DeviceType> {
        if(!mode.read) {
            return Error::Code::FileNotOpened;
        }

        if(data->type != FileType::Device) {
            return DeviceType::None;
        }
        return data->get_device_type();
    }

    auto create_device(const std::string_view name, const uintptr_t device_impl) -> Result<OpenInfo> {
        if(!mode.write) {
            return Error::Code::FileNotOpened;
        }

        return data->create_device(name, device_impl);
    }

    auto control_device(const DeviceOperation op, void* const arg) -> Error {
        return data->control_device(op, arg);
    }

    auto read_event() -> Event& {
        return *write_event;
    }

    auto operator=(Handle&& o) -> Handle& {
        close();
        std::swap(data, o.data);
        mode = o.mode;
        std::swap(write_event, o.write_event);
        return *this;
    }

    Handle() = default;

    Handle(Handle&& o) {
        *this = std::move(o);
    }

    Handle(OpenInfo* const data, const OpenMode mode) : data(data), mode(mode) {
        write_event.reset(new Event());
        data->on_handle_create(*write_event);
    }

    ~Handle() {
        close();
    }
};
} // namespace fs
