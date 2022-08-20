#pragma once
#include <cstddef>

#include "../error.hpp"
#include "../fs/drivers/dev.hpp"

namespace block {
struct DeviceInfo {
    size_t bytes_per_sector;
    size_t total_sectors;
};

template <class P>
concept Parent = std::derived_from<P, fs::dev::BlockDevice> && requires(const P& device) {
    { device.get_info() } -> std::same_as<DeviceInfo>;
};
} // namespace block
