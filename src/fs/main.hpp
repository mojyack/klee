#pragma once
#include "../block/drivers/ahci.hpp"
#include "../block/drivers/cache.hpp"
#include "../block/drivers/partition.hpp"
#include "../block/gpt.hpp"
#include "../kernel-commands.hpp"
#include "../macro.hpp"
#include "control.hpp"
#include "drivers/fat/driver.hpp"
#include "drivers/tmp.hpp"

namespace fs {
namespace mountdev {
struct Disk {
    size_t disk;
};

struct Partition {
    size_t disk;
    size_t part;
};

struct Tmpfs {};

struct Unknown {};

using MountDev = std::variant<Disk, Partition, Tmpfs, Unknown>;
} // namespace mountdev

inline auto str_to_mdev(const std::string_view device) -> mountdev::MountDev {
    if(device.starts_with("disk")) {
        if(device.size() == sizeof("disk?") - 1) {
            return mountdev::Disk{static_cast<size_t>(device[4] - '0')};
        } else if(device.size() == sizeof("disk?p?") - 1) {
            return mountdev::Partition{static_cast<size_t>(device[4] - '0'), static_cast<size_t>(device[6] - '0')};
        } else {
            return mountdev::Unknown{};
        }
    } else if(device == "tmpfs") {
        return mountdev::Tmpfs{};
    } else {
        return mountdev::Unknown{};
    }
}

inline auto mdev_to_str(const mountdev::MountDev& mdev) -> std::string {
    if(std::holds_alternative<mountdev::Disk>(mdev)) {
        const auto& disk = std::get<mountdev::Disk>(mdev).disk;
        auto        buf  = std::array<char, 8>();
        snprintf(buf.data(), buf.size(), "disk%lu", disk);
        return buf.data();
    } else if(std::holds_alternative<mountdev::Partition>(mdev)) {
        const auto& disk = std::get<mountdev::Partition>(mdev);
        auto        buf  = std::array<char, 12>();
        snprintf(buf.data(), buf.size(), "disk%lup%lu", disk.disk, disk.part);
        return buf.data();
    } else if(std::holds_alternative<mountdev::Tmpfs>(mdev)) {
        return "tmpfs";
    } else {
        return "unknown";
    }
}

struct Partition {
    block::gpt::Partition       partition;
    std::unique_ptr<fs::Driver> filesystem;
};

struct SataDevice {
    block::cache::Device<block::ahci::Device> device;
    std::vector<Partition>                    partitions;
};

struct MountRecord {
    fs::Driver*        fs_driver;
    mountdev::MountDev device;
    std::string        path;
};

class FilesystemManager {
  private:
    std::vector<SataDevice>                       sata_devices;
    fs::Controller                                fs;
    std::vector<std::unique_ptr<fs::tmp::Driver>> tmpfs_drivers;
    std::vector<MountRecord>                      mount_records;

  public:
    auto list_block_devices() -> std::vector<std::string> {
        auto r = std::vector<std::string>();
        for(auto d = 0; d < sata_devices.size(); d += 1) {
            for(auto p = 0; p < sata_devices[d].partitions.size(); p += 1) {
                auto buf = std::array<char, 32>();
                snprintf(buf.data(), buf.size(), "disk%dp%d", d, p);
                r.emplace_back(buf.data());
            }
        }
        return r;
    }

    auto mount(const std::string_view device, const std::string_view path) -> Error {
        const auto mdev = str_to_mdev(device);
        if(std::holds_alternative<mountdev::Unknown>(mdev)) {
            return Error::Code::UnknownDevice;
        }
        if(std::holds_alternative<mountdev::Disk>(mdev)) {
            const auto disk = std::get<mountdev::Disk>(mdev).disk;
            if(disk >= sata_devices.size()) {
                return Error::Code::UnknownDevice;
            }
            // TODO
            // assume filesystem
            return Error::Code::UnknownFilesystem;
        }

        auto fs_driver = (fs::Driver*)nullptr;

        if(std::holds_alternative<mountdev::Partition>(mdev)) {
            const auto& part = std::get<mountdev::Partition>(mdev);
            if(part.disk >= sata_devices.size() || part.part >= sata_devices[part.disk].partitions.size()) {
                return Error::Code::IndexOutOfRange;
            }
            auto& p = sata_devices[part.disk].partitions[part.part];
            if(p.filesystem) {
                return Error::Code::AlreadyMounted;
            }

            auto fat_driver = std::unique_ptr<fs::fat::Driver>();
            switch(p.partition.filesystem) {
            case block::gpt::Filesystem::FAT32:
                if(auto e = fs::fat::new_driver(*p.partition.device.get())) {
                    fat_driver = std::move(e.as_value());
                } else {
                    return e.as_error();
                }
                break;
            default:
                return Error::Code::UnknownFilesystem;
            }

            error_or(fs.mount(path, *fat_driver.get()));
            p.filesystem = std::move(fat_driver);
            fs_driver    = p.filesystem.get();
        } else if(std::holds_alternative<mountdev::Tmpfs>(mdev)) {
            auto tmp_driver = std::unique_ptr<fs::tmp::Driver>(new fs::tmp::Driver());
            error_or(fs.mount(path, *tmp_driver.get()));
            fs_driver = tmpfs_drivers.emplace_back(std::move(tmp_driver)).get();
        } else {
            return Error::Code::UnknownFilesystem;
        }

        mount_records.emplace_back(MountRecord{fs_driver, mdev, std::string(path)});
        return Error();
    }

    auto unmount(const std::string_view path) -> Error {
        value_or(freed_device, fs.unmount(path));

        for(auto i = mount_records.begin(); i != mount_records.end(); i += 1) {
            if(i->fs_driver == freed_device) {
                if(std::holds_alternative<mountdev::Tmpfs>(i->device)) {
                    for(auto i = tmpfs_drivers.begin(); i != tmpfs_drivers.end(); i += 1) {
                        if(i->get() == freed_device) {
                            tmpfs_drivers.erase(i);
                            break;
                        }
                    }
                }
                mount_records.erase(i);
                break;
            }
        }

        return Error();
    }

    auto get_mounts() const -> std::vector<commands::MountRecord> {
        auto r = std::vector<commands::MountRecord>();
        r.reserve(mount_records.size());
        for(const auto& m : mount_records) {
            r.emplace_back(commands::MountRecord{mdev_to_str(m.device), m.path});
        }
        return r;
    }

    auto get_fs_root() -> fs::Controller& {
        return fs;
    }

    FilesystemManager(ahci::Controller& controller) {
        for(auto& dev : controller.get_devices()) {
            auto& new_device = sata_devices.emplace_back(SataDevice{block::cache::Device<block::ahci::Device>(dev)});
            auto  partitions = block::gpt::find_partitions(new_device.device);
            if(partitions) {
                for(auto& p : partitions.as_value()) {
                    new_device.partitions.emplace_back(Partition{std::move(p)});
                }
            }
        }
    }
};

inline auto main(const uint64_t id, const int64_t data) -> void {
    auto manager = FilesystemManager(*reinterpret_cast<ahci::Controller*>(data));

    commands::list_blocks = [&manager]() -> std::vector<std::string> {
        return manager.list_block_devices();
    };

    commands::mount = [&manager](const std::string_view device, const std::string_view path) -> Error {
        return manager.mount(device, path);
    };

    commands::unmount = [&manager](const std::string_view path) -> Error {
        return manager.unmount(path);
    };

    commands::get_mounts = [&manager]() -> std::vector<commands::MountRecord> {
        return manager.get_mounts();
    };

    commands::get_filesystem_root = [&manager]() -> fs::Controller& {
        return manager.get_fs_root();
    };

    printk("[fs] initialize done\n");
    auto& this_task = task::task_manager->get_current_task();
    while(true) {
        const auto message = this_task.receive_message();
        if(!message) {
            this_task.sleep();
            continue;
        }
        switch(message->type) {
        default:
            break;
        }
    }
}
} // namespace fs
