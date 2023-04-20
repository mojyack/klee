#pragma once
#include "../../driver.hpp"
#include "../../pagecache.hpp"

namespace fs::dev {
class Device {
  public:
    virtual auto read(uint64_t& handle_data, const size_t block, const size_t count, void* const buffer) -> Result<size_t> {
        return Error::Code::NotSupported;
    }

    virtual auto write(uint64_t& handle_data, const size_t block, const size_t count, const void* const buffer) -> Result<size_t> {
        return Error::Code::NotSupported;
    }

    virtual auto get_filesize() const -> size_t {
        return 0;
    }

    virtual auto create_handle_data() -> Result<uint64_t> {
        return 0;
    }

    virtual auto destroy_handle_data(uint64_t& handle_data) -> Error {
        return Success();
    }

    virtual auto on_handle_create(uint64_t& handle_data) -> void {
    }

    virtual auto on_handle_destroy(uint64_t& handle_data) -> void {
    }

    virtual auto get_write_event(uint64_t& handle_data) -> Event* {
        return nullptr;
    }

    virtual auto get_cache_provider() -> std::shared_ptr<CacheProvider> {
        return std::shared_ptr<CacheProvider>(new DefaultCacheProvider());
    }

    virtual auto get_device_type() const -> DeviceType = 0;

    virtual auto get_attributes() const -> fs::Attributes {
        return fs::default_attributes;
    }

    virtual auto get_blocksize_exp() const -> uint8_t {
        return 0;
    }

    virtual ~Device() {}
};

} // namespace fs::dev
