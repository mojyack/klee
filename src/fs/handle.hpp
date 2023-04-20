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
    FileOperator*    fop;
    PerHandle        per_handle;
    OpenMode         mode;
    std::atomic_bool expired = true;

  public:
    auto read(const auto offset, const size_t size, void* const buffer) -> Result<size_t> {
        if(!mode.read) {
            return Error::Code::FileNotOpened;
        }

        return fop->read(per_handle, offset, size, buffer);
    }

    auto write(const auto offset, const size_t size, const void* const buffer) -> Result<size_t> {
        if(!mode.write) {
            return Error::Code::FileNotOpened;
        }

        return fop->write(per_handle, offset, size, buffer);
    }

    auto open(const std::string_view name, const OpenMode open_mode) -> Result<Handle> {
        if(!mode.read) {
            return Error::Code::FileNotOpened;
        }

        auto created_fop = std::optional<FileOperator>();
        auto result      = (FileOperator*)(nullptr);
        {
            auto [lock, children] = fop->critical_children.access();

            const auto prepare_r = fop->prepare_fop(children, per_handle, name, created_fop);
            if(!prepare_r) {
                return prepare_r.as_error();
            }
            const auto& prepare = prepare_r.as_value();

            result = prepare;

            if(const auto e = try_open(result, open_mode)) {
                return e;
            }
        }

        if(created_fop) {
            result = fop->append_child(std::move(*created_fop));
        }

        return Handle(result, open_mode);
    }

    auto close() -> void;

    auto find(const std::string_view name) -> Result<FileAbstract> {
        if(!mode.read) {
            return Error::Code::FileNotOpened;
        }

        return fop->find(per_handle, name);
    }

    auto create(const std::string_view name, const FileType type) -> Error {
        if(!mode.write) {
            return Error::Code::FileNotOpened;
        }

        const auto r = fop->create(per_handle, name, type);
        return r ? Success() : r.as_error();
    }

    auto readdir(const size_t index) -> Result<FileAbstract> {
        if(!mode.read) {
            return Error::Code::FileNotOpened;
        }

        return fop->readdir(per_handle, index);
    }

    auto remove(const std::string_view name) -> Error {
        if(!mode.write) {
            return Error::Code::FileNotOpened;
        }

        return fop->remove(per_handle, name);
    }

    auto get_filesize() const -> Result<size_t> {
        if(!mode.read) {
            return Error::Code::FileNotOpened;
        }

        return fop->filesize;
    }

    auto get_device_type() const -> Result<DeviceType> {
        if(!mode.read) {
            return Error::Code::FileNotOpened;
        }

        if(fop->type != FileType::Device) {
            return DeviceType::None;
        }
        return fop->get_device_type();
    }

    auto get_blocksize() const -> size_t {
        return size_t(1) << fop->blocksize_exp;
    }

    auto create_device(const std::string_view name, const uintptr_t device_impl) -> Result<FileAbstract> {
        if(!mode.write) {
            return Error::Code::FileNotOpened;
        }

        return fop->create_device(per_handle, name, device_impl);
    }

    auto control_device(const DeviceOperation op, void* const arg) -> Error {
        return fop->control_device(per_handle, op, arg);
    }

    auto get_write_event() -> Result<Event*> {
        const auto event = fop->get_write_event(per_handle);
        if(event == nullptr) {
            return Error::Code::NotSupported;
        }
        return event;
    }

    operator bool() const {
        return !expired;
    }

    auto operator=(Handle&& o) -> Handle& {
        close();
        fop        = o.fop;
        per_handle = o.per_handle;
        mode       = o.mode;
        expired    = o.expired.exchange(true);
        return *this;
    }

    Handle() : expired(true) {
    }

    Handle(Handle&& o) {
        *this = std::move(o);
    }

    Handle(FileOperator* const fop, const OpenMode mode) : fop(fop), mode(mode), expired(false) {
        if(const auto r = fop->create_handle_data(); !r) {
            logger(LogLevel::Error, "fs: failed to create driver data of %s: %d\n", fop->name.data(), r.as_error().as_int());
        } else {
            per_handle.driver_data = r.as_value();
        }
        per_handle.cursor = 0;
        fop->on_handle_create(per_handle);
    }

    ~Handle() {
        close();
    }
};

auto open(const std::string_view path, const OpenMode mode) -> Result<Handle>;
auto close(Handle& handle) -> void;

inline auto Handle::close() -> void {
    ::fs::close(*this);
}
} // namespace fs
