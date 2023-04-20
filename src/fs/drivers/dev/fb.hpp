#pragma once
#include "base.hpp"

namespace fs::dev {
class FramebufferDevice : public Device {
  private:
    auto copy(uint64_t& handle_data, const auto offset, const size_t size, void* const buffer, const bool write) -> Result<size_t> {
        const auto limit = calc_limit();
        if(offset + size >= limit) {
            return Error::Code::EndOfFile;
        }

        const auto copy = std::min(limit - offset, size);
        if(write) {
            memcpy(data + offset, buffer, copy);
        } else {
            memcpy(buffer, data + offset, copy);
        }
        return size_t(copy);
    }

  protected:
    std::byte*            data;
    std::array<size_t, 2> buffer_size;
    Event                 write_event;

    auto calc_limit() const -> size_t {
        return buffer_size[0] * buffer_size[1] * 4;
    }

  public:
    auto read(uint64_t& handle_data, const size_t block, const size_t count, void* const buffer) -> Result<size_t> override {
        return copy(handle_data, block, count, buffer, false);
    }

    auto write(uint64_t& handle_data, const size_t block, const size_t count, const void* const buffer) -> Result<size_t> override {
        return copy(handle_data, block, count, const_cast<void*>(buffer), true);
    }

    // devfs specific
    auto get_filesize() const -> size_t override {
        return calc_limit();
    }

    auto get_write_event(uint64_t& handle_data) -> Event* override {
        return &write_event;
    }

    auto get_device_type() const -> DeviceType override {
        return DeviceType::Framebuffer;
    }

    auto get_attributes() const -> fs::Attributes override {
        return fs::Attributes{
            .read_level    = fs::OpenLevel::Single,
            .write_level   = fs::OpenLevel::Single,
            .exclusive     = true,
            .volume_root   = false,
            .cache         = false,
            .keep_on_close = false,
        };
    }

    // device specific
    auto get_size() -> std::array<size_t, 2> {
        return buffer_size;
    }

    auto direct_access() -> std::byte** {
        return &data;
    }

    virtual auto swap() -> void                     = 0;
    virtual auto is_double_buffered() const -> bool = 0;
};

} // namespace fs::dev
