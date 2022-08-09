#pragma once
#include "../block.hpp"

namespace block::partition {
class PartitionBlockDevice : public BlockDevice {
  private:
    BlockDevice* parent;

    size_t first_sector;
    size_t sector_size;
    size_t total_sectors;

  public:
    auto get_info() -> DeviceInfo override {
        return DeviceInfo{sector_size, total_sectors};
    }

    auto read_sector(const size_t sector, const size_t count, void* const buffer) -> Error override {
        return parent->read_sector(sector + first_sector, count, buffer);
    }

    auto write_sector(size_t sector, size_t count, const void* buffer) -> Error override {
        return parent->write_sector(sector + first_sector, count, buffer);
    }

    PartitionBlockDevice(BlockDevice* const parent, const size_t first_sector, const size_t total_sectors) : parent(parent),
                                                                                                             first_sector(first_sector),
                                                                                                             sector_size(parent->get_info().bytes_per_sector),
                                                                                                             total_sectors(total_sectors) {}
};
} // namespace block::partition
