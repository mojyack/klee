#pragma once
#include "../../fs/drivers/dev.hpp"
#include "../block.hpp"

namespace block::partition {
class PartitionBlockDevice : public fs::dev::BlockDevice {
  private:
    fs::dev::BlockDevice& parent;

    size_t first_sector;

  public:
    auto read_sector(const size_t sector, const size_t count, void* const buffer) -> Error override {
        return parent.read_sector(sector + first_sector, count, buffer);
    }

    auto write_sector(size_t sector, size_t count, const void* buffer) -> Error override {
        return parent.write_sector(sector + first_sector, count, buffer);
    }

    PartitionBlockDevice(fs::dev::BlockDevice& parent, const size_t first_sector, const size_t total_sectors) : parent(parent),
                                                                                                                first_sector(first_sector) {
        this->bytes_per_sector = parent.get_bytes_per_sector();
        this->total_sectors    = total_sectors;
    }
};
} // namespace block::partition
