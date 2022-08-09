#pragma once
#include "../block/drivers/ahci.hpp"
#include "../block/drivers/cache.hpp"
#include "../block/drivers/partition.hpp"
#include "../block/gpt.hpp"
#include "../kernel-commands.hpp"
#include "fs.hpp"

namespace fs {
inline auto main(const uint64_t id, const int64_t data) -> void {
    struct SataDevice {
        block::cache::Device<block::ahci::Device> device;
        std::vector<block::gpt::Partition>        partitions;
    };

    auto sata_devices = std::vector<SataDevice>();

    commands::list_blocks = [&sata_devices]() -> std::vector<std::string> {
        auto r = std::vector<std::string>();
        for(auto d = 0; d < sata_devices.size(); d += 1) {
            for(auto p = 0; p < sata_devices[d].partitions.size(); p += 1) {
                auto buf = std::array<char, 32>();
                snprintf(buf.data(), buf.size(), "disk%dp%d", d, p);
                r.emplace_back(buf.data());
            }
        }
        return r;
    };

    auto& controller = *reinterpret_cast<ahci::Controller*>(data);
    for(auto& dev : controller.get_devices()) {
        auto& new_device = sata_devices.emplace_back(SataDevice{block::cache::Device<block::ahci::Device>(dev)});
        auto  partitions = block::gpt::find_partitions(new_device.device);
        if(partitions) {
            new_device.partitions = std::move(partitions.as_value());
        }
    }

    auto& this_task = task::task_manager->get_current_task();

    printk("[fs] initialize done\n");
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
