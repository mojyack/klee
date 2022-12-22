#pragma once
#include <concepts>
#include <unordered_map>

#include "../../macro.hpp"
#include "../block.hpp"

namespace block::cache {
template <Parent P>
class Device : public fs::dev::BlockDevice {
  private:
    struct SectorCache {
        bool                     dirty = false;
        std::unique_ptr<uint8_t> data;

        SectorCache(const size_t bytes_per_sector) {
            data.reset(new uint8_t[bytes_per_sector]);
        }
    };

    P                                       parent;
    std::unordered_map<size_t, SectorCache> cache;

    auto get_cache(const size_t sector) -> Result<SectorCache*> {
        if(auto p = cache.find(sector); p != cache.end()) {
            return &p->second;
        }

        auto new_cache = SectorCache(bytes_per_sector);
        error_or(parent.read_sector(sector, 1, new_cache.data.get()));

        return &cache.emplace(sector, std::move(new_cache)).first->second;
    }

    auto read_sector(const size_t sector, const size_t count, void* const buffer) -> Error override {
        for(auto i = 0; i < count; i += 1) {
            const auto s      = sector + i;
            auto       result = get_cache(s);
            if(!result) {
                return result.as_error();
            }

            auto& cache = *result.as_value();
            std::memcpy(static_cast<uint8_t*>(buffer) + bytes_per_sector * i, cache.data.get(), bytes_per_sector);
        }

        return Success();
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
            std::memcpy(cache.data.get(), static_cast<const uint8_t*>(buffer) + bytes_per_sector * i, bytes_per_sector);
        }

        return Success();
    }

  public:
    template <class... Args>
    Device(Args&... args) : parent(args...) {
        const auto info  = parent.get_info();
        bytes_per_sector = info.bytes_per_sector;
        total_sectors    = info.total_sectors;
    }
};
} // namespace block::cache
