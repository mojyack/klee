#pragma once
#include "base.hpp"

namespace fs::dev {
class BlockDevice : public Device {
  private:
    size_t                                bytes_per_sector;
    size_t                                total_sectors;
    std::shared_ptr<DefaultCacheProvider> cache_provider;

  public:
    // devfs specific
    auto get_filesize() const -> size_t override {
        return bytes_per_sector * total_sectors;
    }

    auto get_cache_provider() -> std::shared_ptr<CacheProvider> override {
        return cache_provider;
    }

    auto get_device_type() const -> DeviceType override {
        return DeviceType::Block;
    }

    auto get_blocksize_exp() const -> uint8_t override {
        return std::countr_zero(bytes_per_sector);
    }

    // device specific
    auto get_bytes_per_sector() const -> size_t {
        return bytes_per_sector;
    }

    BlockDevice(const size_t bytes_per_sector, const size_t total_sectors)
        : bytes_per_sector(bytes_per_sector),
          total_sectors(total_sectors),
          cache_provider(new DefaultCacheProvider()) {}
};

class PartitionCacheProvider : public CacheProvider {
  private:
    size_t                         page_offset;
    std::shared_ptr<CacheProvider> base;

  public:
    auto lock() -> SmartMutex override {
        return base->lock();
    }

    auto at(const size_t index) -> CachePage& override {
//        debug::println("cache request: ", this, " ", index, "+", page_offset, ",", base->get_capacity());
        return base->at(page_offset + index);
    }

    auto get_capacity() const -> size_t override {
        const auto size = base->get_capacity();
        return size > page_offset ? size - page_offset : 0;
    }

    auto ensure_capacity(const size_t size) -> void override {
        base->ensure_capacity(page_offset + size);
    }

    PartitionCacheProvider(const size_t block_offset, const size_t blocks_per_page, std::shared_ptr<CacheProvider> base)
        : page_offset(block_offset / blocks_per_page),
          base(base) {}
};

class PartitionBlockDevice : public Device {
  private:
    BlockDevice*                   base;
    size_t                         first_sector;
    size_t                         total_sectors;
    std::shared_ptr<CacheProvider> cache_provider;

  public:
    auto read(uint64_t& handle_data, const size_t block, const size_t count, void* const buffer) -> Result<size_t> override {
        base->get_blocksize_exp();
        return base->read(handle_data, block + first_sector, count, buffer);
    }

    auto write(uint64_t& handle_data, const size_t block, const size_t count, const void* const buffer) -> Result<size_t> override {
        return base->write(handle_data, block + first_sector, count, buffer);
    }

    // devfs specific
    auto get_filesize() const -> size_t override {
        return base->get_bytes_per_sector() * total_sectors;
    }

    auto get_cache_provider() -> std::shared_ptr<CacheProvider> override {
        return cache_provider;
    }

    auto get_device_type() const -> DeviceType override {
        return DeviceType::Block;
    }

    auto get_blocksize_exp() const -> uint8_t override {
        return base->get_blocksize_exp();
    }

    PartitionBlockDevice(BlockDevice* const base, const size_t first_sector, const size_t total_sectors, const size_t blocks_per_page)
        : base(base),
          first_sector(first_sector),
          total_sectors(total_sectors),
          cache_provider(new PartitionCacheProvider(first_sector, blocks_per_page, base->get_cache_provider())) {}
};
} // namespace fs::dev
