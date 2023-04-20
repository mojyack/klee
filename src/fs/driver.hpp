#pragma once
#include "../mutex.hpp"
#include "file-abstract.hpp"
#include "pagecache.hpp"

namespace fs {
enum class DeviceType : uint32_t {
    None,
    Framebuffer,
    Keyboard,
    Mouse,
    Block,
};

enum class DeviceOperation {
    // Framebuffer
    GetSize,
    GetDirectPointer,
    Swap,
    IsDoubleBuffered,
    // Block
    GetBytesPerSector,
};

struct FileAbstractWithDriverData {
    FileAbstract abstract;
    uint64_t     driver_data;
};

class Driver {
  public:
    // read "count" block(=2^FileAbstract::blocksize_exp) from device to "buffer"
    // move internal cursor on success
    // returns number of read blocks or error
    virtual auto read(const uint64_t fop_data, uint64_t& handle_data, const size_t block, const size_t count, void* const buffer) -> Result<size_t> {
        return Error::Code::NotSupported;
    }

    // write "count" block(=2^FileAbstract::blocksize_exp) from "buffer" to "buffer"
    // move internal cursor on success
    // returns number of wrote blocks or error
    virtual auto write(const uint64_t fop_data, uint64_t& handle_data, const size_t block, const size_t count, const void* const buffer) -> Result<size_t> {
        return Error::Code::NotSupported;
    }

    virtual auto find(uint64_t fop_data, uint64_t& handle_data, std::string_view name) -> Result<FileAbstractWithDriverData>                  = 0;
    virtual auto create(uint64_t fop_data, uint64_t& handle_data, std::string_view name, FileType type) -> Result<FileAbstractWithDriverData> = 0;
    virtual auto readdir(uint64_t fop_data, uint64_t& handle_data, size_t index) -> Result<FileAbstractWithDriverData>                        = 0;
    virtual auto remove(uint64_t fop_data, uint64_t& handle_data, std::string_view name) -> Error                                             = 0;

    virtual auto get_device_type(const uint64_t fop_data) -> DeviceType {
        return DeviceType::None;
    }

    virtual auto create_device(uint64_t fop_data, uint64_t& handle_data, std::string_view name, uintptr_t device_impl) -> Result<FileAbstractWithDriverData> {
        return Error::Code::NotImplemented;
    }

    virtual auto control_device(const uint64_t fop_data, uint64_t& handle_data, const DeviceOperation op, void* const arg) -> Error {
        return Error::Code::NotImplemented;
    }

    virtual auto destroy_fop_data(const uint64_t fop_data) -> Error {
        return Success();
    }

    virtual auto create_handle_data(const uint64_t fop_data) -> Result<uint64_t> {
        return 0;
    }

    virtual auto destroy_handle_data(const uint64_t fop_data, uint64_t& handle_data) -> Error {
        return Success();
    }

    virtual auto on_handle_create(const uint64_t fop_data, uint64_t& handle_data) -> void {
    }

    virtual auto on_handle_destroy(const uint64_t fop_data, uint64_t& handle_data) -> void {
    }

    virtual auto get_write_event(const uint64_t fop_data, uint64_t& handle_data) -> Event* {
        return nullptr;
    }

    virtual auto get_cache_provider(const uint64_t fop_data) -> std::shared_ptr<CacheProvider> {
        return std::shared_ptr<CacheProvider>(new DefaultCacheProvider());
    }

    virtual auto get_root() -> FileAbstractWithDriverData& = 0;

    virtual ~Driver() = default;
};
} // namespace fs
