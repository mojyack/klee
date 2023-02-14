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

inline auto follow_mountpoints(fs::FileOperator* fop) -> fs::FileOperator* {
    while(true) {
        const auto mount = fop->mount;
        if(mount != nullptr) {
            fop = mount;
        } else {
            break;
        }
    }
    return fop;
}

inline auto try_open(fs::FileOperator* const fop, const OpenMode mode) -> Error {
    auto [lock, counts] = fop->critical_counts.access();

    if(mode.read) {
        switch(fop->attributes.read_level) {
        case OpenLevel::Block:
            return Error::Code::InvalidOpenMode;
        case OpenLevel::Single:
            if(counts.read_count != 0) {
                return Error::Code::FileOpened;
            }
            [[fallthrough]];
        case OpenLevel::Multi:
            if(fop->attributes.exclusive && counts.write_count != 0) {
                return Error::Code::FileOpened;
            }
            break;
        }
    }
    if(mode.write) {
        switch(fop->attributes.write_level) {
        case OpenLevel::Block:
            return Error::Code::InvalidOpenMode;
        case OpenLevel::Single:
            if(counts.write_count != 0) {
                return Error::Code::FileOpened;
            }
            [[fallthrough]];
        case OpenLevel::Multi:
            if(fop->attributes.exclusive && counts.read_count != 0) {
                return Error::Code::FileOpened;
            }
            break;
        }
    }

    if(mode.read) {
        counts.read_count += 1;
    }
    if(mode.write) {
        counts.write_count += 1;
    }

    return Success();
}

class Handle {
    friend class Manager;

  private:
    FileOperator*          data;
    OpenMode               mode;
    std::unique_ptr<Event> write_event; // data available
    std::atomic_bool       expired = true;

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

        auto created_fop = std::optional<FileOperator>();
        auto result      = (FileOperator*)(nullptr);
        {
            auto [lock, children] = data->critical_children.access();
            if(const auto p = children.find(std::string(name)); p != children.end()) {
                result = follow_mountpoints(&p->second);
            } else {
                auto find_result = data->find(name);
                if(!find_result) {
                    return find_result.as_error();
                }
                result = &created_fop.emplace(std::move(find_result.as_value()));
            }

            if(const auto e = try_open(result, open_mode)) {
                return e;
            }
        }

        if(created_fop) {
            auto [lock, children] = data->critical_children.access();

            auto& v = created_fop.value();
            result  = &children.emplace(v.name, std::move(v)).first->second;
        }

        return Handle(result, open_mode);
    }

    auto find(const std::string_view name) -> Result<FileOperator> {
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

    auto readdir(const size_t index) -> Result<FileOperator> {
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

    auto create_device(const std::string_view name, const uintptr_t device_impl) -> Result<FileOperator> {
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
        data = o.data;
        mode = o.mode;
        std::swap(write_event, o.write_event);
        expired = o.expired.exchange(true);
        return *this;
    }

    Handle() : expired(true) {
    }

    Handle(Handle&& o) {
        *this = std::move(o);
    }

    Handle(FileOperator* const data, const OpenMode mode) : data(data), mode(mode), expired(false) {
        write_event.reset(new Event());
        data->on_handle_create(*write_event);
    }

    ~Handle() {
        close();
    }
};
} // namespace fs
