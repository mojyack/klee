#pragma once
#include "../block/drivers/ahci.hpp"
#include "../block/drivers/cache.hpp"
#include "../block/drivers/partition.hpp"
#include "../block/gpt.hpp"
#include "../macro.hpp"
#include "drivers/dev.hpp"
#include "drivers/tmp.hpp"
#include "handle.hpp"

namespace fs {
struct SataDevice {
    block::cache::Device<block::ahci::Device> device;
    std::vector<block::gpt::Partition>        partitions;
};

class FilesystemManager {
  private:
    struct MountRecord {
        std::string                 device;
        std::string                 mountpoint;
        std::unique_ptr<fs::Driver> fs_driver;
        bool                        shared_driver;

        auto operator=(MountRecord&&) -> MountRecord& = default;

        MountRecord(std::string device, std::string mountpoint, fs::Driver* const fs_driver, const bool shared_driver) : device(std::move(device)),
                                                                                                                         mountpoint(std::move(mountpoint)),
                                                                                                                         fs_driver(std::unique_ptr<fs::Driver>(fs_driver)),
                                                                                                                         shared_driver(shared_driver) {}

        MountRecord(MountRecord&& o) {
            *this = std::move(o);
        }

        ~MountRecord() {
            if(shared_driver) {
                [[maybe_unused]] const auto shared = fs_driver.release();
            }
        }
    };

    std::vector<SataDevice>                       sata_devices;
    fs::Controller                                fs;
    dev::Driver                                   devfs_driver;
    std::vector<std::unique_ptr<fs::tmp::Driver>> tmpfs_drivers;
    std::vector<MountRecord>                      mount_records;

  public:
    // in impl
    auto mount(const std::string_view device, const std::string_view mountpoint) -> Error;

    auto unmount(const std::string_view mountpoint) -> Error {
        value_or(freed_device, fs.unmount(mountpoint));

        for(auto i = mount_records.rbegin(); i != mount_records.rend(); i += 1) {
            if(i->mountpoint != mountpoint) {
                continue;
            }
            if(i->device == "tmpfs") {
                for(auto i = tmpfs_drivers.begin(); i != tmpfs_drivers.end(); i += 1) {
                    if(i->get() == freed_device) {
                        tmpfs_drivers.erase(i);
                        break;
                    }
                }
            }
            mount_records.erase(std::next(i).base());
            break;
        }

        return Error();
    }

    auto get_mounts() const -> std::vector<std::array<std::string, 2>> {
        auto r = std::vector<std::array<std::string, 2>>(mount_records.size());
        for(auto i = 0; i < mount_records.size(); i += 1) {
            r[i][0] = mount_records[i].device;
            r[i][1] = mount_records[i].mountpoint;
        }
        return r;
    }

    auto get_fs_root() -> fs::Controller& {
        return fs;
    }

    auto set_sata_devices(std::vector<SataDevice> devices) -> Error {
        sata_devices = std::move(devices);
        auto buf     = std::array<char, 32>();
        for(auto i = 0; i < sata_devices.size(); i += 1) {
            auto&      d   = sata_devices[i];
            const auto len = snprintf(buf.data(), buf.size(), "disk%d", i);
            error_or(create_device_file({buf.data(), size_t(len)}, &d.device));
            for(auto j = 0; j < d.partitions.size(); j += 1) {
                auto&      p   = d.partitions[j];
                const auto len = snprintf(buf.data(), buf.size(), "disk%dp%d", i, j);
                error_or(create_device_file({buf.data(), size_t(len)}, p.device.get()));
            }
        }
        return Error();
    }

    // just a helper
    auto create_device_file(const std::string_view name, fs::dev::Device* device_impl) -> Error {
        auto& root        = get_fs_root();
        auto  open_result = root.open("/dev", fs::OpenMode::Write);
        if(!open_result) {
            logger(LogLevel::Error, "failed to open \"/dev\": %d\n", open_result.as_error().as_int());
            return open_result.as_error();
        }
        auto& dev = open_result.as_value();
        dev.create_device(name, reinterpret_cast<uintptr_t>(device_impl));
        root.close(std::move(dev));
        return Error();
    }
};

inline auto manager = (Critical<FilesystemManager>*)(nullptr);

inline auto device_finder_main(const uint64_t id, const int64_t data) -> void {
    {
        auto& ahci_controller = *reinterpret_cast<ahci::Controller*>(data);
        auto  sata_devices    = std::vector<SataDevice>();

        ahci_controller.wait_identify();

        for(auto& dev : ahci_controller.get_devices()) {
            auto& new_device = sata_devices.emplace_back(SataDevice{block::cache::Device<block::ahci::Device>(dev)});
            auto  partitions = block::gpt::find_partitions(new_device.device);
            if(partitions) {
                for(auto& p : partitions.as_value()) {
                    new_device.partitions.emplace_back(std::move(p));
                }
            }
        }

        auto [lock, man] = manager->access();
        // TODO
        // pass exit code
        [[maybe_unused]] const auto exit_code = man.set_sata_devices(std::move(sata_devices)) == success;
    }

    task::manager->get_current_task().exit();
}
} // namespace fs
