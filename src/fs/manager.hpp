#pragma once
#include "../block/drivers/ahci.hpp"
#include "../block/drivers/cache.hpp"
#include "../block/drivers/partition.hpp"
#include "../block/gpt.hpp"
#include "drivers/basic.hpp"
#include "drivers/dev.hpp"
#include "drivers/fat/driver.hpp"
#include "drivers/tmp.hpp"
#include "handle.hpp"

namespace fs {
struct SataDevice {
    block::cache::Device<block::ahci::Device> device;
    std::vector<block::gpt::Partition>        partitions;
};

class Manager {
  private:
    struct MountRecord {
        std::string                 device;
        std::string                 mountpoint_path;
        std::unique_ptr<fs::Driver> fs_driver;
        Handle                      mountpoint_handle;
        bool                        shared_driver;

        auto operator=(MountRecord&&) -> MountRecord& = default;

        MountRecord(std::string device, std::string mountpoint, Handle handle, fs::Driver* const fs_driver, const bool shared_driver) : device(std::move(device)),
                                                                                                                                        mountpoint_path(std::move(mountpoint)),
                                                                                                                                        fs_driver(std::unique_ptr<fs::Driver>(fs_driver)),
                                                                                                                                        mountpoint_handle(std::move(handle)),
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

    std::vector<SataDevice> sata_devices;
    basic::Driver           basic_driver;
    dev::Driver             devfs_driver;

    OpenInfo&                          root;
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
        auto info = follow_mountpoints(&root);
        if(const auto e = try_open(info, mode)) {
            return e;
        }
        auto handle = Handle(info, mode);
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

    auto set_mount_driver(const std::string_view path, Driver& driver) -> Result<Handle> {
        auto volume_root = &driver.get_root();
        auto handle_r    = open(path, open_rw);
        if(!handle_r) {
            return handle_r.as_error();
        }
        auto& handle       = handle_r.as_value();
        handle.data->mount = volume_root;
        return std::move(handle);
    }

    auto create_fat_driver(const std::string_view device) -> Result<std::unique_ptr<fat::Driver>> {
        auto handle_r = open(device, open_rw);
        if(!handle_r) {
            return handle_r.as_error();
        }
        auto& handle = handle_r.as_value();

        auto driver = std::unique_ptr<fat::Driver>(new fat::Driver(std::move(handle)));
        if(const auto e = driver->init()) {
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

        auto node = handle.data;
        node->on_handle_destroy();
        handle.write_event.reset();

        {
            auto [lock, counts] = node->critical_counts.access();
            if(handle.mode.read) {
                counts.read_count -= 1;
            }
            if(handle.mode.write) {
                counts.write_count -= 1;
            }
        }
        while(node->parent != nullptr) {
            if(node->is_busy() || node->attributes.volume_root) {
                break;
            }

            const auto parent     = node->parent;
            auto [lock, children] = parent->critical_children.access();
            children.erase(node->name);
            node = parent;
        }
    }

    auto mount(const std::string_view device, const std::string_view mountpoint_path) -> Error {
        auto mountpoint_handle = Handle();
        auto fs_driver         = (fs::Driver*)nullptr;
        auto shared_driver     = false;

        if(device == "devfs") {
            auto handle_r = set_mount_driver(mountpoint_path, devfs_driver);
            if(!handle_r) {
                return handle_r.as_error();
            }
            mountpoint_handle = std::move(handle_r.as_value());
            fs_driver         = &devfs_driver;
            shared_driver     = true;
        } else if(device == "tmpfs") {
            auto driver   = std::unique_ptr<fs::tmp::Driver>(new fs::tmp::Driver());
            auto handle_r = set_mount_driver(mountpoint_path, *driver.get());
            if(!handle_r) {
                return handle_r.as_error();
            }
            mountpoint_handle = std::move(handle_r.as_value());
            fs_driver         = driver.release();
        } else {
            // TODO
            // detect filesystem
            const auto filesystem = block::gpt::Filesystem::FAT32;
            switch(filesystem) {
            case block::gpt::Filesystem::FAT32: {
                auto driver_r = create_fat_driver(device);
                if(!driver_r) {
                    return driver_r.as_error();
                }
                auto& driver = driver_r.as_value();

                auto handle_r = set_mount_driver(mountpoint_path, *driver.get());
                if(!handle_r) {
                    return handle_r.as_error();
                }
                mountpoint_handle = std::move(handle_r.as_value());
                fs_driver         = driver.release();
            } break;
            }
        }

        auto [lock, mount_records] = critical_mount_records.access();
        mount_records.emplace_back(std::string(device), normalize_path(mountpoint_path), std::move(mountpoint_handle), fs_driver, shared_driver);
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
            const auto mountpoint        = mountpoint_handle.data;

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
            auto&      d   = sata_devices[i];
            const auto len = snprintf(buf.data(), buf.size(), "disk%d", i);
            if(const auto e = create_device_file({buf.data(), size_t(len)}, &d.device)) {
                return e;
            }
            for(auto j = 0; j < d.partitions.size(); j += 1) {
                auto&      p   = d.partitions[j];
                const auto len = snprintf(buf.data(), buf.size(), "disk%dp%d", i, j);
                if(const auto e = create_device_file({buf.data(), size_t(len)}, p.device.get())) {
                    return e;
                }
            }
        }
        return Success();
    }

    // just a helper
    auto create_device_file(const std::string_view name, fs::dev::Device* device_impl) -> Error {
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

    Manager() : root(basic_driver.get_root()) {}
};

inline auto manager = (Manager*)(nullptr);

inline auto Handle::close() -> void {
    manager->close(*this);
}

inline fat::BlockDevice::~BlockDevice() {
    manager->close(handle);
}

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

        // TODO
        // pass exit code
        [[maybe_unused]] const auto exit_code = manager->set_sata_devices(std::move(sata_devices)).as_int();
    }

    process::manager->post_kernel_message_with_cli(MessageType::DeviceFinderDone);
    process::manager->exit_this_thread();
}
} // namespace fs
