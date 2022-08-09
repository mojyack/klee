#pragma once
#include <vector>

#include "../../../block/block.hpp"
#include "../../../macro.hpp"
#include "../../fs.hpp"
#include "fat.hpp"

namespace fs::fat {

#define assert(c, e) \
    if(!(c)) {       \
        return e;    \
    }

struct DirectoryInfo {
    uint32_t    cluster;
    uint32_t    size;
    std::string name;
    Attribute   attribute;
};

class ClusterOperator {
  private:
    const BPB::Summary& bpb;
    block::BlockDevice& block;

    template <bool write>
    auto cluster_operation(const size_t cluster, std::conditional_t<write, const uint8_t*, uint8_t*> buffer) -> Error {
        const auto fat_start  = bpb.reserved_sector_count;
        const auto fat_last   = fat_start + bpb.fat_size_32 * bpb.num_fats - 1;
        const auto data_start = fat_last + 1;
        const auto data_last  = bpb.total_sectors_32 - 1;

        const auto sector = data_start + (cluster - 2) * bpb.sectors_per_cluster;
        if((sector + bpb.sectors_per_cluster - 1) > data_last) {
            return Error::Code::IndexOutOfRange;
        }
        if constexpr(write) {
            return block.write_sector(sector, bpb.sectors_per_cluster, buffer);
        } else {
            return block.read_sector(sector, bpb.sectors_per_cluster, buffer);
        }
    }

  public:
    auto read_cluster(const size_t cluster, uint8_t* const buffer) -> Error {
        return cluster_operation<false>(cluster, buffer);
    }

    auto write_cluster(const size_t cluster, const uint8_t* const buffer) -> Error {
        return cluster_operation<true>(cluster, buffer);
    }

    auto get_cluster_size_bytes() const -> size_t {
        return block.get_info().bytes_per_sector * bpb.sectors_per_cluster;
    }

    ClusterOperator(const BPB::Summary& bpb, block::BlockDevice& block) : bpb(bpb), block(block) {}
};

constexpr auto end_of_cluster_chain = 0x0FFFFFF8;

inline auto read_fat_for_cluster(const uint32_t cluster, const BPB::Summary& bpb, block::BlockDevice& block) -> uint32_t {
    // fat[0] and fat[1] are reserved

    const auto sector = bpb.reserved_sector_count + (cluster * 4 / bpb.bytes_per_sector);
    const auto offset = cluster * 4 % bpb.bytes_per_sector;

    auto buffer = std::vector<uint8_t>(block.get_info().bytes_per_sector);
    block.read_sector(sector, 1, buffer.data());
    return *reinterpret_cast<uint32_t*>(buffer.data() + offset);
}

inline auto increment_fat(uint32_t& cluster, const uint32_t count, const BPB::Summary& bpb, block::BlockDevice& block) -> bool {
    for(auto i = size_t(0); i < count; i += 1) {
        if(cluster == end_of_cluster_chain) {
            return false;
        }
        cluster = read_fat_for_cluster(cluster, bpb, block);
    }
    return true;
}

class DirectoryIterator {
  private:
    uint32_t            cluster;
    uint32_t            index;
    const BPB::Summary& bpb;
    block::BlockDevice& block;
    ClusterOperator     op;

  public:
    auto skip(const size_t count) -> Error {
        if(count == 0) {
            return Error();
        }

        const auto cluster_size_bytes         = op.get_cluster_size_bytes();
        const auto directory_entry_table_size = cluster_size_bytes / sizeof(DirectoryEntry);

        auto count_current = 0;
        auto buffer        = std::vector<uint8_t>(cluster_size_bytes);

        while(true) { // iterate over clusters(fats)
            error_or(op.read_cluster(cluster, buffer.data()));

            while(index < directory_entry_table_size) { // iterate over directory entries
                auto& entry = *reinterpret_cast<DirectoryEntry*>(buffer.data() + sizeof(DirectoryEntry) * (index % directory_entry_table_size));
                index += 1;
                if(entry.name[0] == 0xE5) {
                    continue;
                } else if(entry.name[0] == 0x00) {
                    return Error::Code::EndOfFile;
                }
                if(entry.attr & Attribute::LongName) {
                    continue;
                }

                count_current += 1;
                if(count_current == count) {
                    return Error();
                }
            }

            if(!increment_fat(cluster, 1, bpb, block)) {
                return Error::Code::EndOfFile;
            }
        }
        return Error::Code::EndOfFile;
    }

    auto read() -> Result<DirectoryInfo> {
        const auto cluster_size_bytes         = op.get_cluster_size_bytes();
        const auto directory_entry_table_size = cluster_size_bytes / sizeof(DirectoryEntry);

        auto lfn_checksum = 0;
        auto lfn          = std::u16string();
        auto buffer       = std::vector<uint8_t>(cluster_size_bytes);

        while(true) { // iterate over clusters(fats)
            error_or(op.read_cluster(cluster, buffer.data()));

            while(index < directory_entry_table_size) { // iterate over directory entries
                auto& entry = *reinterpret_cast<DirectoryEntry*>(buffer.data() + sizeof(DirectoryEntry) * (index % directory_entry_table_size));
                index += 1;
                if(entry.name[0] == 0xE5) {
                    continue;
                } else if(entry.name[0] == 0x00) {
                    return Error::Code::EndOfFile;
                }
                if(entry.attr & Attribute::LongName) {
                    auto& lfn_entry = *reinterpret_cast<LFNEntry*>(&entry);
                    if(lfn_entry.number & 0x40) {
                        lfn_checksum = lfn_entry.checksum;
                        lfn.clear();
                    }
                    assert(lfn_checksum == lfn_entry.checksum, Error::Code::BadChecksum);
                    lfn = lfn_entry.to_string() + lfn;
                    continue;
                }

                auto r      = DirectoryInfo();
                r.cluster   = (static_cast<uint32_t>(entry.first_cluster_high) << 16) | entry.first_cluster_low;
                r.size      = entry.file_size;
                r.attribute = entry.attr;
                if(lfn_checksum == entry.calc_checksum()) {
                    auto name = std::string();
                    name.resize(lfn.size());
                    for(auto i = size_t(0); i < lfn.size(); i += 1) {
                        name[i] = static_cast<char>(lfn[i]);
                    }
                    r.name = std::move(name);
                } else {
                    r.name = entry.to_string();
                }
                return r;
            }

            if(!increment_fat(cluster, 1, bpb, block)) {
                return Error::Code::EndOfFile;
            }
        }
        return Error::Code::EndOfFile;
    }

    DirectoryIterator(const uint32_t first_cluster, const BPB::Summary& bpb, block::BlockDevice& block) : cluster(first_cluster),
                                                                                                          index(0),
                                                                                                          bpb(bpb),
                                                                                                          block(block),
                                                                                                          op(bpb, block) {}
};

class Driver : public fs::Driver {
  private:
    block::BlockDevice* block;
    BPB::Summary        bpb;

    OpenInfo root;

    auto openinfo_from_dinfo(const DirectoryInfo& d) -> OpenInfo {
        const auto type    = d.attribute & Attribute::Directory ? FileType::Directory : FileType::Regular;
        const auto cluster = d.cluster == 0 ? bpb.root_cluster : d.cluster;
        const auto size    = type == FileType::Directory ? 0 : d.size;
        return OpenInfo(d.name, *this, cluster, type, size);
    }

  public:
    auto init() -> Error {
        auto buffer = std::vector<uint8_t>(block->get_info().bytes_per_sector);
        error_or(block->read_sector(0, 1, buffer.data()));
        const auto& bpb = *reinterpret_cast<BPB*>(buffer.data());
        assert(bpb.signature[0] == 0x55 && bpb.signature[1] == 0xAA, Error::Code::NotFAT);
        assert(bpb.bytes_per_sector == block->get_info().bytes_per_sector, Error::Code::NotImplemented);

        this->bpb  = bpb.summary();
        this->root = OpenInfo("/", *this, this->bpb.root_cluster, FileType::Directory, true);

        return Error();
    }

    auto read(const DriverData data, const size_t offset, size_t size, void* const buffer_) -> Error override {
        if(data.type != FileType::Regular) {
            return Error::Code::InvalidData;
        }

        if(offset + size > data.size) {
            return Error::Code::EndOfFile;
        }

        auto       op                = ClusterOperator(bpb, *block);
        auto       buffer            = static_cast<uint8_t*>(buffer_);
        const auto bytes_per_cluster = op.get_cluster_size_bytes();
        auto       cluster           = static_cast<uint32_t>(data.num);
        if(!increment_fat(cluster, offset / bytes_per_cluster, bpb, *block)) {
            return Error::Code::EndOfFile;
        }
        auto read_buffer = std::vector<uint8_t>(bytes_per_cluster);

        {
            const auto offset_in_cluster = offset % bytes_per_cluster;
            const auto size_in_cluster   = bytes_per_cluster - offset_in_cluster;
            const auto copy_len          = size < size_in_cluster ? size : size_in_cluster;
            error_or(op.read_cluster(cluster, read_buffer.data()));
            memcpy(buffer, read_buffer.data() + offset_in_cluster, copy_len);
            buffer += copy_len;
            size -= copy_len;
            if(!increment_fat(cluster, 1, bpb, *block)) {
                return Error::Code::EndOfFile;
            }
        }

        while(size >= bytes_per_cluster) {
            error_or(op.read_cluster(cluster, read_buffer.data()));
            memcpy(buffer, read_buffer.data(), bytes_per_cluster);
            buffer += bytes_per_cluster;
            size -= bytes_per_cluster;
            if(size != 0 && !increment_fat(cluster, 1, bpb, *block)) {
                return Error::Code::EndOfFile;
            }
        }

        if(size != 0) {
            error_or(op.read_cluster(cluster, read_buffer.data()));
            memcpy(buffer, read_buffer.data(), size);
        }

        return Error();
    }

    auto write(const DriverData data, const size_t offset, const size_t size, const void* const buffer) -> Error override {
        return Error::Code::NotImplemented;
    }

    auto find(const DriverData data, const std::string_view name) -> Result<OpenInfo> override {
        if(data.type != FileType::Directory) {
            return Error::Code::InvalidData;
        }
        auto iterator = DirectoryIterator(static_cast<size_t>(data.num), bpb, *block);
        while(true) {
            const auto dinfo_result = iterator.read();
            if(!dinfo_result) {
                const auto e = dinfo_result.as_error();
                if(e == Error::Code::EndOfFile) {
                    return Error::Code::NoSuchFile;
                } else {
                    return e;
                }
            }
            const auto& dinfo = dinfo_result.as_value();
            if(dinfo.name == name) {
                return openinfo_from_dinfo(dinfo);
            }
        }
        return Error::Code::NoSuchFile;
    }

    auto create(const DriverData data, const std::string_view name, const FileType type) -> Result<OpenInfo> override {
        return Error::Code::NotImplemented;
    }

    auto readdir(const DriverData data, const size_t index) -> Result<OpenInfo> override {
        if(data.type != FileType::Directory) {
            return Error::Code::InvalidData;
        }
        auto iterator = DirectoryIterator(static_cast<size_t>(data.num), bpb, *block);
        if(iterator.skip(index)) {
            return Error::Code::IndexOutOfRange;
        }
        value_or(dinfo, iterator.read());

        return openinfo_from_dinfo(dinfo);
    }

    auto remove(const DriverData data, const std::string_view name) -> Error override {
        return Error::Code::NotImplemented;
    }

    auto get_root() -> OpenInfo& override {
        return root;
    }

    Driver(block::BlockDevice& block) : block(&block),
                                        root("/", *this, nullptr, FileType::Directory, 0, true) {}
};

inline auto new_driver(block::BlockDevice& block) -> Result<std::unique_ptr<Driver>> {
    auto driver = std::unique_ptr<Driver>(new Driver(block));
    error_or(driver->init());
    return driver;
}

#undef error_or
#undef assert
} // namespace fs::fat
