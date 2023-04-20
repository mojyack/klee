#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../error.hpp"
#include "../log.hpp"
#include "../memory/allocator.hpp"
#include "../mutex.hpp"
#include "../process/manager.hpp"
#include "../util/string-map.hpp"
#include "driver.hpp"
#include "file-abstract.hpp"
#include "pagecache.hpp"

namespace fs {
struct PerHandle {
    uint64_t cursor;
    uint64_t driver_data;
};

class FileOperator;

auto follow_mountpoints(FileOperator* fop) -> FileOperator*;

class FileOperator {
  private:
    Driver* const driver;
    uint64_t      driver_data = 0;

    std::shared_ptr<CacheProvider> cache_provider;
    // Critical<std::vector<CachePage>> critical_cache;

    static constexpr auto ceil(const std::integral auto a, const std::integral auto b) -> auto{
        return (a + b - 1) / b;
    }

    enum class Initialize {
        None,
        All,
        HeadTail,
    };

    auto prepare_cache(uint64_t& handle_driver_data, const size_t begin, const size_t end, const Initialize initialize) -> Error {
        const auto blocksize = size_t(1) << blocksize_exp;
        // debug::println("  prepare_cache ", begin, " ", end, " bs=", blocksize);
        if(cache_provider->get_capacity() < end) {
            cache_provider->ensure_capacity(end);
        }
        if(blocksize > paging::bytes_per_page) {
            // TODO
            // support block device which block size is larger than page size
            return Error::Code::NotImplemented;
        } else {
            const auto blocks_per_page = paging::bytes_per_page >> blocksize_exp;
            for(auto p = begin; p < end; p += 1) {
                auto& cache = cache_provider->at(p);
                // debug::println("    page ", p);
                switch(cache.state) {
                case CachePage::State::Uninitialized: {
                    // debug::println("      allocating");
                    auto page_r = memory::allocate_single();
                    if(!page_r) {
                        // debug::println("      fail ", page_r.as_error());
                        return page_r.as_error();
                    }
                    auto& page = page_r.as_value();
                    // debug::println("      done ", page->get_id());

                    if((initialize == Initialize::All || (initialize == Initialize::HeadTail && (p == begin || p == end - 1))) &&
                       p * paging::bytes_per_page < filesize) {
                        const auto filesize_blocks = ceil(filesize, blocksize);
                        const auto block_begin     = p * blocks_per_page;
                        const auto blocks_read     = std::min(blocks_per_page, filesize_blocks - block_begin);
                        // debug::println("reading ", blocks_read * blocksize, " bytes to ", page->get_frame());
                        if(const auto r = driver->read(driver_data, handle_driver_data, block_begin, blocks_read, page->get_frame()); !r) {
                            return r.as_error();
                        }
                    }

                    cache.page  = std::move(page);
                    cache.state = CachePage::State::Clean;
                } break;
                case CachePage::State::Dirty:
                    // debug::println("      dirty");
                    break;
                case CachePage::State::Clean:
                    // debug::println("      clean");
                    break;
                }
            }
        }
        return Success();
    }

    auto copy(PerHandle& per_handle, const size_t offset, const size_t size, void* const buffer, const bool write) -> Result<size_t> {
        // if cache is disabled, pass through to driver's implementation
        if(!attributes.cache) {
            const auto mask = (size_t(1) << blocksize_exp) - 1;
            if(mask & offset || mask & size) {
                return Error::Code::InvalidSize;
            }
            if(const auto r =
                   write
                       ? driver->write(driver_data, per_handle.driver_data, offset >> blocksize_exp, size >> blocksize_exp, buffer)
                       : driver->read(driver_data, per_handle.driver_data, offset >> blocksize_exp, size >> blocksize_exp, buffer);
               !r) {
                return r.as_error();
            } else {
                return r.as_value() << blocksize_exp;
            }
        }
        // debug::println("copy ", offset, "+", size);

        if(size == 0) {
            return 0;
        }

        // check for overrun
        if(offset + size > filesize) {
            return Error::Code::IndexOutOfRange;
        }

        // prepare cache
        const auto cache_begin = offset / paging::bytes_per_page;
        const auto cache_end   = ceil(offset + size, paging::bytes_per_page);

        auto lock = cache_provider->lock();
        if(const auto e = prepare_cache(per_handle.driver_data, cache_begin, cache_end, write ? Initialize::HeadTail : Initialize::All)) {
            return e;
        }

        // perform read
        const auto copyable = std::min(size, filesize - offset);
        auto       copy     = copyable;
        auto       buf      = std::bit_cast<std::byte*>(buffer);
        auto       page     = cache_begin;
        // - align cursor
        if(offset % paging::bytes_per_page != 0) {
            const auto offset_in_page = offset % paging::bytes_per_page;
            const auto size_in_page   = paging::bytes_per_page - offset_in_page;
            const auto len            = std::min(copyable, size_in_page);
            auto&      cache          = cache_provider->at(page);
            // debug::println("  head ", page, "+", offset_in_page, " ", len, " @", cache.get_frame(), " of ", cache_provider.get());
            if(write) {
                memcpy(cache.get_frame() + offset_in_page, buf, len);
            } else {
                memcpy(buf, cache.get_frame() + offset_in_page, len);
            }
            buf += len;
            copy -= len;
            page += 1;
        }
        // - copy page by page
        if(copy != 0) {
            while(copy >= paging::bytes_per_page) {
                const auto len   = paging::bytes_per_page;
                auto&      cache = cache_provider->at(page);
                // debug::println("  body ", page, " ", len);
                if(write) {
                    memcpy(cache.get_frame(), buf, len);
                } else {
                    memcpy(buf, cache.get_frame(), len);
                }
                buf += len;
                copy -= len;
                page += 1;
            }
        }
        // - copy the rest
        if(copy != 0) {
            auto& cache = cache_provider->at(page);
            if(write) {
                memcpy(cache.get_frame(), buf, copy);
            } else {
                memcpy(buf, cache.get_frame(), copy);
            }
            // debug::println("  tail ", page, " ", copy);
        }

        // debug::println("  done ", copyable);
        return copyable;
    }

    auto extract_abstract(Result<FileAbstractWithDriverData> abstract) -> Result<FileAbstract> {
        if(!abstract) {
            return abstract.as_error();
        }
        return abstract.as_value().abstract;
    }

  public:
    struct Count {
        uint32_t read_count  = 0;
        uint32_t write_count = 0;
    };

    using Children = StringMap<FileOperator>;

    size_t             filesize;
    FileOperator*      parent;
    FileOperator*      mount = nullptr;
    const std::string  name;
    const FileType     type;
    const BlockSizeExp blocksize_exp;
    const Attributes   attributes;

    Critical<Count>    critical_counts;
    Critical<Children> critical_children;

    auto read(PerHandle& per_handle, const size_t offset, const size_t size, void* const buffer) -> Result<size_t> {
        return copy(per_handle, offset, size, buffer, false);
    }

    auto write(PerHandle& per_handle, const size_t offset, const size_t size, const void* const buffer) -> Result<size_t> {
        return copy(per_handle, offset, size, const_cast<void*>(buffer), true);
    }

    /*
    auto resize() -> void {
        const auto new_data_size = (new_size + memory::bytes_per_frame - 1) / memory::bytes_per_frame;
        const auto old_data_size = data.size();
        if(new_data_size > old_data_size) {
            auto new_frames = std::vector<memory::SmartSingleFrameID>(new_data_size - old_data_size);
            for(auto& f : new_frames) {
                auto frame_r = memory::allocate_single();
                if(!frame_r) {
                    return frame_r.as_error();
                }
                auto& frame = frame_r.as_value();

                f = std::move(frame);
            }
            data.reserve(new_data_size);
            std::move(std::begin(new_frames), std::end(new_frames), std::back_inserter(data));
        } else if(new_data_size < old_data_size) {
            data.resize(new_data_size);
        }
        filesize = new_size;
        return Success();
    }
    */

    auto find(PerHandle& per_handle, const std::string_view name) -> Result<FileAbstract> {
        auto [lock, children] = critical_children.access();
        if(const auto p = children.find(std::string(name)); p != children.end()) {
            return p->second.build_abstract();
        }

        return extract_abstract(driver->find(driver_data, per_handle.driver_data, name));
    }

    auto create(PerHandle& per_handle, const std::string_view name, const FileType type) -> Result<FileAbstract> {
        return extract_abstract(driver->create(driver_data, per_handle.driver_data, name, type));
    }

    auto readdir(PerHandle& per_handle, const size_t index) -> Result<FileAbstract> {
        return extract_abstract(driver->readdir(driver_data, per_handle.driver_data, index));
    }

    auto remove(PerHandle& per_handle, const std::string_view name) -> Error {
        auto [lock, children] = critical_children.access();
        if(const auto p = children.find(name); p == children.end()) {
            return driver->remove(driver_data, per_handle.driver_data, name);
        } else {
            auto& child = p->second;
            if(child.is_busy()) {
                return Error::Code::FileOpened;
            }
            if(const auto e = driver->remove(driver_data, per_handle.driver_data, name)) {
                return e;
            } else {
                children.erase(p);
                return Success();
            }
        }
    }

    auto get_device_type() -> DeviceType { // can be used without opening
        if(type != FileType::Device) {
            return DeviceType::None;
        }

        return driver->get_device_type(driver_data);
    }

    auto create_device(PerHandle& per_handle, const std::string_view name, const uintptr_t device_impl) -> Result<FileAbstract> {
        return extract_abstract(driver->create_device(driver_data, per_handle.driver_data, name, device_impl));
    }

    auto control_device(PerHandle& per_handle, const DeviceOperation op, void* const arg) -> Error {
        return driver->control_device(driver_data, per_handle.driver_data, op, arg);
    }

    auto create_handle_data() -> Result<uint64_t> {
        return driver->create_handle_data(driver_data);
    }

    auto destroy_per_handle(PerHandle& per_handle) -> Error {
        return driver->destroy_handle_data(driver_data, per_handle.driver_data);
    }

    auto get_write_event(PerHandle& per_handle) -> Event* {
        return driver->get_write_event(driver_data, per_handle.driver_data);
    }

    // used by Handle
    auto on_handle_create(PerHandle& per_handle) -> void {
        driver->on_handle_create(driver_data, per_handle.driver_data);
    }

    auto on_handle_destroy(PerHandle& per_handle) -> void {
        driver->on_handle_destroy(driver_data, per_handle.driver_data);
    }

    auto build_abstract() const -> FileAbstract {
        return FileAbstract{name, filesize, type, blocksize_exp, attributes};
    }

    auto prepare_fop(Children& children, PerHandle& per_handle, const std::string_view name, std::optional<FileOperator>& storage) -> Result<FileOperator*> {
        if(const auto p = children.find(std::string(name)); p != children.end()) {
            return follow_mountpoints(&p->second);
        }

        const auto find_r = driver->find(driver_data, per_handle.driver_data, name);
        if(!find_r) {
            return find_r.as_error();
        }
        const auto& find = find_r.as_value();

        return &storage.emplace(FileOperator(*driver, find));
    }

    auto append_child(FileOperator&& child) -> FileOperator* {
        auto [lock, children] = critical_children.access();
        return &children.emplace(child.name, std::move(child)).first->second;
    }

    // used by Controller
    auto is_busy() -> bool {
        if(mount != nullptr) {
            return true;
        }

        {
            auto [lock, counts] = critical_counts.access();
            if(counts.read_count != 0 || counts.write_count != 0) {
                return true;
            }
        }

        {
            auto [lock, children] = critical_children.access();
            if(!children.empty()) {
                return true;
            }
        }

        return false;
    }

    FileOperator(FileOperator&& other)
        : driver(other.driver),
          driver_data(std::exchange(other.driver_data, 0)),
          cache_provider(std::move(other.cache_provider)),
          filesize(other.filesize),
          parent(other.parent),
          mount(other.mount),
          name(std::move(other.name)),
          type(other.type),
          blocksize_exp(other.blocksize_exp),
          attributes(other.attributes),
          critical_counts(std::move(other.critical_counts)),
          critical_children(std::move(other.critical_children)) {
    }

    FileOperator(Driver& driver, FileAbstractWithDriverData abstract)
        : FileOperator(driver, abstract.driver_data, abstract.abstract) {}

    FileOperator(Driver& driver, const uint64_t driver_data, FileAbstract abstract)
        : driver(&driver),
          driver_data(driver_data),
          cache_provider(abstract.attributes.cache ? driver.get_cache_provider(driver_data) : nullptr),
          filesize(abstract.filesize),
          name(std::move(abstract.name)),
          type(abstract.type),
          blocksize_exp(abstract.blocksize_exp),
          attributes(abstract.attributes) {
    }

    ~FileOperator() {
        if(driver_data != 0) {
            if(const auto e = driver->destroy_fop_data(driver_data)) {
                logger(LogLevel::Error, "fs: failed to destroy driver data %d\n", e.as_int());
            }
        }
    }
};

inline auto follow_mountpoints(FileOperator* fop) -> FileOperator* {
    while(true) {
        const auto mount = fop->mount;
        if(mount != nullptr) {
            fop = mount;
        } else {
            break;
        }
    }
    return fop;
}
} // namespace fs
