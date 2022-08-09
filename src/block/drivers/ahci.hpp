#pragma once
#include "../../ahci/ahci.hpp"
#include "../block.hpp"

namespace block::ahci {
class Device : public BlockDevice {
  private:
    size_t              bytes_per_sector;
    ::ahci::SATADevice* device;

  public:
    auto get_info() -> DeviceInfo override {
        const auto i = device->get_info();
        return {i.bytes_per_sector, i.total_sectors};
    }

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

    Device(::ahci::SATADevice& device) : device(&device) {
        bytes_per_sector = device.get_info().bytes_per_sector;
    }
};
} // namespace block::ahci
