#pragma once
#include "../block/drivers/ahci.hpp"
#include "../block/gpt.hpp"
#include "drivers/basic.hpp"
#include "drivers/dev/driver.hpp"
#include "drivers/fat/driver.hpp"
#include "drivers/tmp.hpp"
#include "handle.hpp"

namespace fs {
struct SataDevice {
    block::ahci::Device                                     device;
    std::vector<std::unique_ptr<dev::PartitionBlockDevice>> partitions;
};

class Manager {
  private:
    struct MountRecord {
        std::string                   device;
        std::string                   mountpoint_path;
        std::unique_ptr<Driver>       driver;
        std::unique_ptr<FileOperator> root;
        Handle                        mountpoint_handle;
        bool                          shared_driver;

        auto operator=(MountRecord&&) -> MountRecord& = default;

        MountRecord(std::string device, std::string mountpoint, Handle mountpoint_handle, Driver* const driver, FileOperator* const root, const bool shared_driver)
            : device(std::move(device)),
              mountpoint_path(std::move(mountpoint)),
              driver(std::unique_ptr<Driver>(driver)),
              root(std::unique_ptr<FileOperator>(root)),
              mountpoint_handle(std::move(mountpoint_handle)),
              shared_driver(shared_driver) {}

        MountRecord(MountRecord&& o) {
            *this = std::move(o);
        }

        ~MountRecord() {
            if(shared_driver) {
                [[maybe_unused]] const auto shared1 = driver.release();
                [[maybe_unused]] const auto shared2 = root.release();
            }
        }
    };

    std::vector<SataDevice> sata_devices;
    basic::Driver           basic_driver;
    FileOperator            basic_root;
    dev::Driver             devfs_driver;
    FileOperator            devfs_root;
    FileOperator&           root;

    Critical<std::vector<MountRecord>> critical_mount_records;

    static auto split_path(const std::string_view path) -> std::vector<std::string_view> {
        auto r = std::vector<std::string_view>();

        auto end   = size_t(0);
        auto start = size_t();
        while((start = path.find_first_not_of('/', end)) != std::string_view::npos) {
            end = path.find('/', start);
            r.emplace_back(path.substr(start, end - start));
        }
        return r;
    }

    static auto normalize_path(const std::string_view path) -> std::string {
        auto r = std::string();
        for(const auto e : split_path(path)) {
            r += "/";
            r += e;
        }
        if(r.empty()) {
            r = "/";
        }
        return r;
    }

    auto open_root(const OpenMode mode) -> Result<Handle> {
        auto fop = follow_mountpoints(&root);
        if(const auto e = try_open(fop, mode)) {
            return e;
        }
        auto handle = Handle(fop, mode);
        return handle;
    }

    auto open_parent_directory(std::vector<std::string_view>& elms) -> Result<Handle> {
        if(elms.empty()) {
            return open_root(open_ro);
        }

        auto dirname = std::span<std::string_view>(elms.begin(), elms.size() - 1);
        auto result  = open_root(open_ro);
        if(!result) {
            return result.as_error();
        }

        for(const auto& d : dirname) {
            auto handle = std::move(result.as_value());
            result      = handle.open(d, open_ro);
            close(handle);
            if(!result) {
                return result;
            }
        }
        return result;
    }

    auto set_mount_driver(const std::string_view path, FileOperator& root) -> Result<Handle> {
        auto handle_r = open(path, open_rw);
        if(!handle_r) {
            return handle_r.as_error();
        }
        auto& handle = handle_r.as_value();

        handle.fop->mount = &root;
        return std::move(handle);
    }

    auto create_fat_driver(const std::string_view device) -> Result<std::unique_ptr<fat::Driver>> {
        auto handle_r = open(device, open_rw);
        if(!handle_r) {
            return handle_r.as_error();
        }
        auto& handle = handle_r.as_value();

        auto driver = std::unique_ptr<fat::Driver>(new fat::Driver());
        if(const auto e = driver->init(std::move(handle))) {
            return e;
        }

        return driver;
    }

  public:
    auto open(const std::string_view path, const OpenMode mode) -> Result<Handle> {
        auto elms = split_path(path);
        if(elms.empty()) {
            return open_root(mode);
        }

        const auto filename = elms.back();
        auto       parent_r = open_parent_directory(elms);
        if(!parent_r) {
            return parent_r.as_error();
        }
        auto& parent = parent_r.as_value();
        auto  result = parent.open(filename, mode);
        close(parent);
        return result;
    }

    auto close(Handle& handle) -> void {
        const auto expired = handle.expired.exchange(true);
        if(expired) {
            return;
        }

        auto fop = handle.fop;
        fop->on_handle_destroy(handle.per_handle);
        if(const auto e = fop->destroy_per_handle(handle.per_handle)) {
            logger(LogLevel::Error, "fs: failed to destroy handle data: %d\n", e.as_int());
            return;
        }

        {
            auto [lock, counts] = fop->critical_counts.access();
            if(handle.mode.read) {
                counts.read_count -= 1;
            }
            if(handle.mode.write) {
                counts.write_count -= 1;
            }
        }
        while(fop->parent != nullptr) {
            if(fop->is_busy() || fop->attributes.volume_root || fop->attributes.keep_on_close) {
                break;
            }

            const auto parent     = fop->parent;
            auto [lock, children] = parent->critical_children.access();
            children.erase(fop->name);
            fop = parent;
        }
    }

    auto mount(const std::string_view device, const std::string_view mountpoint_path) -> Error {
        auto mountpoint_handle = Handle();
        auto driver            = std::unique_ptr<Driver>();
        auto root              = std::unique_ptr<FileOperator>();
        auto shared_driver     = false;

        if(device == "devfs") {
            auto handle_r = set_mount_driver(mountpoint_path, devfs_root);
            if(!handle_r) {
                return handle_r.as_error();
            }
            mountpoint_handle = std::move(handle_r.as_value());
            driver.reset(&devfs_driver);
            root.reset(&devfs_root);
            shared_driver = true;
        } else if(device == "tmpfs") {
            auto tmpfs_driver = std::unique_ptr<Driver>(new tmp::Driver());
            auto tmpfs_root   = std::unique_ptr<FileOperator>(new FileOperator(*tmpfs_driver, tmpfs_driver->get_root()));

            auto handle_r = set_mount_driver(mountpoint_path, *tmpfs_root.get());
            if(!handle_r) {
                return handle_r.as_error();
            }
            mountpoint_handle = std::move(handle_r.as_value());

            driver = std::move(tmpfs_driver);
            root   = std::move(tmpfs_root);
        } else {
            // TODO
            // detect filesystem
            const auto filesystem = block::gpt::Filesystem::FAT32;
            switch(filesystem) {
            case block::gpt::Filesystem::FAT32: {
                auto fatfs_driver_r = create_fat_driver(device);
                if(!fatfs_driver_r) {
                    return fatfs_driver_r.as_error();
                }
                auto& fatfs_driver = fatfs_driver_r.as_value();

                auto fatfs_root = std::unique_ptr<FileOperator>(new FileOperator(*fatfs_driver, fatfs_driver->get_root()));

                auto handle_r = set_mount_driver(mountpoint_path, *fatfs_root.get());
                if(!handle_r) {
                    return handle_r.as_error();
                }
                mountpoint_handle = std::move(handle_r.as_value());

                driver = std::move(fatfs_driver);
                root   = std::move(fatfs_root);
            } break;
            }
        }

        auto [lock, mount_records] = critical_mount_records.access();
        mount_records.emplace_back(std::string(device), normalize_path(mountpoint_path), std::move(mountpoint_handle), driver.release(), root.release(), shared_driver);
        return Success();
    }

    auto unmount(const std::string_view mountpoint_path) -> Error {
        const auto path            = normalize_path(mountpoint_path);
        auto [lock, mount_records] = critical_mount_records.access();
        for(auto i = mount_records.rbegin(); i != mount_records.rend(); i += 1) {
            if(i->mountpoint_path != path) {
                continue;
            }

            auto&      mountpoint_handle = i->mountpoint_handle;
            const auto mountpoint        = mountpoint_handle.fop;

            const auto volume_root = mountpoint->mount;
            if(volume_root->is_busy()) {
                return Error::Code::VolumeBusy;
            }
            mountpoint->mount = nullptr;
            close(i->mountpoint_handle);
            mount_records.erase(std::next(i).base());
            return Success();
        }

        return Error::Code::NotMounted;
    }

    auto get_mounts() const -> std::vector<std::array<std::string, 2>> {
        auto [lock, mount_records] = critical_mount_records.access();

        auto r = std::vector<std::array<std::string, 2>>(mount_records.size());
        for(auto i = 0; i < mount_records.size(); i += 1) {
            r[i][0] = mount_records[i].device;
            r[i][1] = mount_records[i].mountpoint_path;
        }
        return r;
    }

    auto set_sata_devices(std::vector<SataDevice> devices) -> Error {
        sata_devices = std::move(devices);
        auto buf     = std::array<char, 32>();
        for(auto i = 0; i < sata_devices.size(); i += 1) {
            auto&      satadev     = sata_devices[i];
            const auto len         = snprintf(buf.data(), buf.size(), "disk%d", i);
            const auto device_name = std::string_view{buf.data(), size_t(len)};
            if(const auto e = create_device_file(device_name, &satadev.device)) {
                return e;
            }

            const auto bytes_per_sector = satadev.device.get_bytes_per_sector();
            if(bytes_per_sector > memory::bytes_per_frame) {
                logger(LogLevel::Warn, "fs: block size of device %s is larger than page size and not supported\n", std::string(device_name).data());
                continue;
            }
            const auto blocks_per_page = memory::bytes_per_frame / bytes_per_sector;

            const auto path = std::string("/dev/") + std::string(device_name);

            auto partitions_r = block::gpt::find_partitions(path);
            if(!partitions_r) {
                logger(LogLevel::Error, "fs: failed to find partitions: %d", partitions_r.as_error().as_int());
                continue;
            }
            auto& partitions = partitions_r.as_value();

            for(auto j = 0; j < partitions.size(); j += 1) {
                if(partitions[j].lba_start % blocks_per_page != 0) {
                    logger(LogLevel::Warn, "fs: partition %d of device %s is not page aligned and not supported\n", j, std::string(device_name).data());
                }
                auto partdev = std::unique_ptr<dev::PartitionBlockDevice>(new dev::PartitionBlockDevice(&satadev.device, partitions[j].lba_start, partitions[i].lba_last - partitions[i].lba_start + 1, blocks_per_page));

                const auto len         = snprintf(buf.data(), buf.size(), "disk%dp%d", i, j);
                const auto device_name = std::string_view{buf.data(), size_t(len)};
                if(const auto e = create_device_file(device_name, partdev.get())) {
                    logger(LogLevel::Error, "fs: failed to create partition device file: %d", e.as_int());
                    continue;
                }

                satadev.partitions.emplace_back(std::move(partdev));
            }
        }

        return Success();
    }

    // just a helper
    auto create_device_file(const std::string_view name, dev::Device* device_impl) -> Error {
        auto dev_r = open("/dev", open_rw);
        if(!dev_r) {
            logger(LogLevel::Error, "failed to open \"/dev\": %d\n", dev_r.as_error().as_int());
            return dev_r.as_error();
        }
        auto& dev = dev_r.as_value();

        dev.create_device(name, std::bit_cast<uintptr_t>(device_impl));
        close(dev);
        return Success();
    }

    Manager() : basic_root(basic_driver, basic_driver.get_root()),
                devfs_root(devfs_driver, devfs_driver.get_root()),
                root(basic_root) {}
};

inline auto manager = (Manager*)(nullptr);

inline auto open(const std::string_view path, const OpenMode mode) -> Result<Handle> {
    return manager->open(path, mode);
}

inline auto close(Handle& handle) -> void {
    return manager->close(handle);
}

inline auto device_finder_main(const uint64_t id, const int64_t data) -> void {
    {
        auto& ahci_controller = *reinterpret_cast<ahci::Controller*>(data);
        auto  sata_devices    = std::vector<SataDevice>();

        ahci_controller.wait_identify();

        for(auto& dev : ahci_controller.get_devices()) {
            sata_devices.emplace_back(SataDevice{block::ahci::Device(dev)});
        }

        // TODO
        // pass exit code
        [[maybe_unused]] const auto exit_code = manager->set_sata_devices(std::move(sata_devices)).as_int();
    }

    process::manager->post_kernel_message_with_cli(MessageType::DeviceFinderDone);
    process::manager->exit_this_thread();
}
} // namespace fs
