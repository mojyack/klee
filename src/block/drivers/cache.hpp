#pragma once
#include <concepts>
#include <unordered_map>

#include "../../macro.hpp"
#include "../block.hpp"

namespace block::cache {
template <class P>
concept Parent = std::derived_from<P, BlockDevice>;

template <Parent P>
class Device : public BlockDevice {
  private:
    struct SectorCache {
        bool                     dirty = false;
        std::unique_ptr<uint8_t> data;

        SectorCache(const size_t sector_size) {
            data.reset(new uint8_t[sector_size]);
        }
    };

    P                                       parent;
    size_t                                  sector_size;
    std::unordered_map<size_t, SectorCache> cache;

    auto get_cache(const size_t sector) -> Result<SectorCache*> {
        if(auto p = cache.find(sector); p != cache.end()) {
            return &p->second;
        }

        auto new_cache = SectorCache(sector_size);
        error_or(parent.read_sector(sector, 1, new_cache.data.get()));
        return &cache.emplace(sector, std::move(new_cache)).first->second;
    }

  public:
    auto get_info() -> DeviceInfo override {
        return parent.get_info();
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

    template <class... Args>
    Device(Args&... args) : parent(args...) {
        sector_size = parent.get_info().bytes_per_sector;
    }
};
} // namespace block::cache
