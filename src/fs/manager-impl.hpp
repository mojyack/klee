#pragma once
#include "../fs/drivers/fat/create.hpp"

namespace fs {
inline auto FilesystemManager::mount(const std::string_view device, const std::string_view mountpoint) -> Error {
    auto fs_driver     = (fs::Driver*)nullptr;
    auto shared_driver = true;

    if(device == "devfs") {
        error_or(fs.mount(mountpoint, devfs_driver));
        fs_driver = &devfs_driver;
    } else if(device == "tmpfs") {
        auto tmp_driver = std::unique_ptr<fs::tmp::Driver>(new fs::tmp::Driver());
        error_or(fs.mount(mountpoint, *tmp_driver.get()));
        fs_driver = tmpfs_drivers.emplace_back(std::move(tmp_driver)).get();
    } else {
        // TODO
        // detect filesystem
        const auto filesystem = block::gpt::Filesystem::FAT32;
        switch(filesystem) {
        case block::gpt::Filesystem::FAT32: {
            auto driver_r = fs::fat::new_driver(device);
            if(!driver_r) {
                return driver_r.as_error();
            }
            auto& driver = driver_r.as_value();

            if(const auto e = fs.mount(mountpoint, *driver)) {
                return e;
            }
            fs_driver = driver.release();
        } break;
        }
        shared_driver = false;
    }

    mount_records.emplace_back(std::string(device), std::string(mountpoint), fs_driver, shared_driver);
    return Success();
}
} // namespace fs
