#pragma once
#include "../../ahci/ahci.hpp"
#include "../../fs/drivers/dev/block.hpp"

namespace block::ahci {
class Device : public fs::dev::BlockDevice {
  private:
    ::ahci::SATADevice* device;

  public:
    auto read(uint64_t& handle_data, const size_t block, const size_t count, void* const buffer) -> Result<size_t> override {
        auto event = Event();
        if(!device->read(block, count, static_cast<uint8_t*>(buffer), /*hack*/ count * (size_t(1) << get_blocksize_exp()), event)) {
            return Error::Code::IOError;
        }
        event.wait();
        return count;
    }

    auto write(uint64_t& handle_data, const size_t block, const size_t count, const void* const buffer) -> Result<size_t> override {
        auto event = Event();
        if(!device->write(block, count, static_cast<const uint8_t*>(buffer), /*hack*/ count * (size_t(1) << get_blocksize_exp()), event)) {
            return Error::Code::IOError;
        }
        event.wait();
        return count;
    }

    Device(::ahci::SATADevice& device)
        : fs::dev::BlockDevice(device.get_info().bytes_per_sector, device.get_info().total_sectors),
          device(&device) {}
};
} // namespace block::ahci
