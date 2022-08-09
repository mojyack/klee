#pragma once
#include <array>
#include <fstream>
#include <string_view>
#include <unordered_map>

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

    auto get_cache(const size_t sector) -> Result<SectorCache*> {
        if(auto p = cache.find(sector); p != cache.end()) {
            return &p->second;
        }

        auto new_cache = SectorCache();
        if(const auto e = read_file(sector, new_cache.data.get())) {
            return e;
        }
        return &cache.emplace(sector, std::move(new_cache)).first->second;
    }

  public:
    auto get_info() -> DeviceInfo override {
        return DeviceInfo{sector_size, total_sectors};
    }

    auto read_sector(const size_t sector, const size_t count, void* const buffer) -> Error override {
        for(auto i = 0; i < count; i += 1) {
            const auto s      = sector + i;
            auto       result = get_cache(s);
            if(!result) {
                return result.as_error();
            }

            auto& cache = *result.as_value();
            std::memcpy(static_cast<uint8_t*>(buffer) + sector_size * i, cache.data.get(), sector_size);
        }

        return Error();
    }

    auto write_sector(size_t sector, size_t count, const void* buffer) -> Error override {
        for(auto i = 0; i < count; i += 1) {
            const auto s      = sector + i;
            auto       result = get_cache(s);
            if(!result) {
                return result.as_error();
            }

            auto& cache = *result.as_value();

            cache.dirty = true;
            std::memcpy(cache.data.get(), static_cast<const uint8_t*>(buffer) + sector_size * i, sector_size);
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
