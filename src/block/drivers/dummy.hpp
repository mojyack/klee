#pragma once
#include <array>
#include <fstream>
#include <string_view>
#include <unordered_map>

#include "../../macro.hpp"
#include "../block.hpp"

namespace block::dummy {
class DummyBlockDevice : public BlockDevice {
  private:
    static constexpr auto sector_size = 512;

    struct SectorCache {
        bool                     dirty = false;
        std::unique_ptr<uint8_t> data  = std::unique_ptr<uint8_t>(new uint8_t[sector_size]);
    };

    std::fstream                            file;
    size_t                                  filesize;
    size_t                                  total_sectors;
    std::unordered_map<size_t, SectorCache> cache;

    auto read_file(const size_t sector, uint8_t* const buffer) -> Error {
        if(sector >= total_sectors) {
            return Error::Code::InvalidSector;
        }

        file.seekg(sector * sector_size, std::ios::beg);
        file.read(reinterpret_cast<char*>(buffer), sector_size);

        if(file.fail()) {
            return Error::Code::IOError;
        }

        return Error();
    }

    auto write_file(const size_t sector, const uint8_t* const buffer) -> Error {
        if(sector >= total_sectors) {
            return Error::Code::InvalidSector;
        }

        file.seekp(sector * sector_size);
        file.write(reinterpret_cast<const char*>(buffer), sector_size);

        if(file.fail()) {
            return Error::Code::IOError;
        }

        return Error();
    }

  public:
    auto get_info() -> DeviceInfo override {
        return DeviceInfo{sector_size, total_sectors};
    }

    auto read_sector(const size_t sector, const size_t count, void* const buffer) -> Error override {
        for(auto i = 0; i < count; i += 1) {
            const auto s = sector + i;
            error_or(read_file(s, static_cast<uint8_t*>(buffer) + sector_size * i));
        }

        return Error();
    }

    auto write_sector(size_t sector, size_t count, const void* buffer) -> Error override {
        for(auto i = 0; i < count; i += 1) {
            const auto s = sector + i;
            error_or(write_file(s, static_cast<const uint8_t*>(buffer) + sector_size * i));
        }

        return Error();
    }

    DummyBlockDevice(const std::string_view path) {
        file.open(path);
        file.seekg(0, std::ios::end);
        filesize      = file.tellg();
        total_sectors = filesize / sector_size;
    }
};
} // namespace block::dummy
