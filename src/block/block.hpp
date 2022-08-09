#pragma once
#include <cstddef>

#include "../error.hpp"

namespace block {
struct DeviceInfo {
    size_t bytes_per_sector;
    size_t total_sectors;
};

class BlockDevice {
  public:
    virtual auto get_info() -> DeviceInfo                                               = 0;
    virtual auto read_sector(size_t sector, size_t count, void* buffer) -> Error        = 0;
    virtual auto write_sector(size_t sector, size_t count, const void* buffer) -> Error = 0;

    virtual ~BlockDevice() = default;
};
} // namespace block
