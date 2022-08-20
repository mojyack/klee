#pragma once
#include "../../ahci/ahci.hpp"
#include "../block.hpp"

namespace block::ahci {
class Device : public fs::dev::BlockDevice {
  private:
    ::ahci::SATADevice* device;

  public:
    auto read_sector(const size_t sector, const size_t count, void* const buffer) -> Error override {
        auto event = Event();
        if(!device->read(sector, count, static_cast<uint8_t*>(buffer), /*hack*/ count * bytes_per_sector, event)) {
            return Error::Code::IOError;
        }
        event.wait();
        return Error();
    }

    auto write_sector(size_t sector, size_t count, const void* const buffer) -> Error override {
        auto event = Event();
        if(!device->write(sector, count, static_cast<const uint8_t*>(buffer), /*hack*/ count * bytes_per_sector, event)) {
            return Error::Code::IOError;
        }
        event.wait();
        return Error();
    }

    auto get_info() const -> DeviceInfo {
        return {bytes_per_sector, total_sectors};
    }

    Device(::ahci::SATADevice& device) : device(&device) {
        const auto info  = device.get_info();
        bytes_per_sector = info.bytes_per_sector;
        total_sectors    = info.total_sectors;
    }
};
} // namespace block::ahci
