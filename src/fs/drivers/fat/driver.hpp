#pragma once
#include <vector>

#include "../../../util/encoding.hpp"
#include "../../handle.hpp"
#include "fat.hpp"

namespace fs::fat {

struct DirectoryInfo {
    uint32_t    cluster;
    uint32_t    size;
    std::string name;
    Attribute   attribute;
};

enum class FATEntryType {
    Free,
    Reserved,
    Used,
    Bad,
    Last,
};

constexpr auto eof_cluster = uint32_t(0x0FFF'FFF8);

constexpr auto get_fat_entry_type(const uint32_t fate) -> FATEntryType {
    if(fate == 0x0000'0000) {
        return FATEntryType::Free;
    } else if(fate == 0x0000'0001) {
        return FATEntryType::Reserved;
    } else if(fate == 0x0FFF'FFF7) {
        return FATEntryType::Bad;
    } else if(fate >= 0x0FFF'FFF8 && fate <= 0x0FFF'FFFF) {
        return FATEntryType::Last;
    } else {
        return FATEntryType::Used;
    }
};

struct FOPData {
    FileType type : 4;
    uint32_t cluster : 28;
    uint32_t filesize : 32;

    static_assert(static_cast<uint32_t>(FileType::FileTypeEnd) <= 0b1000);
};

struct HandleData {
    uint32_t current_cluster;
    uint32_t current_cluster_number;

    static auto from_fop_data(const FOPData data) -> HandleData {
        return HandleData{
            .current_cluster        = data.cluster,
            .current_cluster_number = 0,
        };
    }
};

struct HandleDirectoryData {
    uint32_t current_cluster;
    uint16_t current_index;
    uint16_t dentry_index;

    static auto from_fop_data(const FOPData data) -> HandleDirectoryData {
        return HandleDirectoryData{
            .current_cluster = data.cluster,
            .current_index   = 0,
            .dentry_index    = 0,
        };
    }
};

class Driver : public fs::Driver {
  private:
    BPBSummary bpb;
    Handle     device;

    std::optional<FileAbstractWithDriverData> root;

    auto calc_blocksize_exp() const -> uint8_t {
        return std::countr_zero(size_t(bpb.bytes_per_sector) * bpb.sectors_per_cluster);
    }

    auto file_abstract_from_dinfo(const DirectoryInfo& dinfo) -> FileAbstractWithDriverData {
        const auto type    = dinfo.attribute & Attribute::Directory ? FileType::Directory : FileType::Regular;
        const auto cluster = dinfo.cluster == 0 ? bpb.root_cluster : dinfo.cluster;
        const auto size    = type == FileType::Directory ? 0 : dinfo.size;
        const auto data    = FOPData{.type = type, .cluster = cluster, .filesize = size};
        return FileAbstractWithDriverData{{dinfo.name, size, type, calc_blocksize_exp(), fs::default_attributes}, std::bit_cast<uint64_t>(data)};
    }

    auto device_io(const uint64_t pos, const uint64_t size, void* const buffer, const bool write) -> Error {
        if(const auto r = write ? device.write(pos, size, buffer) : device.read(pos, size, buffer); !r) {
            return r.as_error();
        } else if(r.as_value() != size) {
            return Error::Code::IOError;
        }
        return Success();
    }

    // copy data at cluster.offset.
    // the copy range must fit within a single cluster.
    // assume that:
    //   offset < bytes_per_cluster
    auto copy_cluster(const uint32_t cluster, const uint32_t offset, const uint32_t size, void* const buffer, const bool write) -> Error {
        // in sectors
        const auto fat_begin     = bpb.reserved_sector_count;
        const auto fat_end       = fat_begin + bpb.fat_size_32 * bpb.num_fats;
        const auto data_begin    = fat_end;
        const auto data_end      = bpb.total_sectors_32;
        const auto cluster_begin = data_begin + (cluster - 2) * bpb.sectors_per_cluster;

        if(cluster_begin + size / bpb.bytes_per_sector >= data_end) {
            return Error::Code::IndexOutOfRange;
        }

        // in bytes
        const auto copy_begin = cluster_begin * bpb.bytes_per_sector + offset;

        return device_io(copy_begin, size, buffer, write);
    }

    auto read_fat_entry(const uint32_t cluster) -> Result<uint32_t> {
        const auto fat_begin = bpb.reserved_sector_count;
        const auto sector    = fat_begin + (cluster * sizeof(uint32_t) / bpb.bytes_per_sector);
        const auto offset    = cluster * sizeof(uint32_t) % bpb.bytes_per_sector;

        auto data = uint32_t();
        if(const auto e = device_io(sector * bpb.bytes_per_sector + offset, sizeof(uint32_t), &data, false)) {
            return e;
        }
        return data & 0x0FFFFFFF;
    }

    auto get_next_cluster(const uint32_t cluster) -> Result<uint32_t> {
        const auto fate_r = read_fat_entry(cluster);
        if(!fate_r) {
            return fate_r.as_error();
        }
        const auto fate = fate_r.as_value();

        switch(get_fat_entry_type(fate)) {
        case FATEntryType::Used:
            return fate;
        case FATEntryType::Last:
            return Error::Code::EndOfFile;
        default:
            logger(LogLevel::Error, "fs: fat: unexpected fat entry type(%d), volume is broken.\n", get_fat_entry_type(fate));
            return Error::Code::BrokenFATEntry;
        }
    }

    auto seek_cluster_chain(const FOPData fop_data, HandleData& handle_data, const uint32_t new_cluster_number) -> Error {
        auto current_cluster        = handle_data.current_cluster;
        auto current_cluster_number = handle_data.current_cluster_number;

        if(new_cluster_number < current_cluster_number) {
            current_cluster        = fop_data.cluster;
            current_cluster_number = 0;
        }

        for(; current_cluster_number < new_cluster_number; current_cluster_number += 1) {
            const auto next_cluster_r = get_next_cluster(current_cluster);
            if(!next_cluster_r) {
                return next_cluster_r.as_error();
            }

            current_cluster = next_cluster_r.as_value();
        }

        handle_data.current_cluster        = current_cluster;
        handle_data.current_cluster_number = current_cluster_number;
        return Success();
    }

    auto readdir(const FOPData data, HandleDirectoryData& handle, const size_t index) -> Result<DirectoryInfo> {
        const auto bytes_per_cluster = bpb.bytes_per_sector * bpb.sectors_per_cluster;

        auto current_cluster      = handle.current_cluster;
        auto current_index        = handle.current_index;
        auto current_dentry_index = handle.dentry_index;

        if(current_cluster == eof_cluster) {
            return Error::Code::EndOfFile;
        }

        if(current_index > index) {
            current_cluster      = data.cluster;
            current_index        = 0;
            current_dentry_index = 0;
        }

        auto lfn_checksum = uint8_t(0);
        auto lfn          = std::u16string();
        auto result       = std::optional<DirectoryInfo>();

    loop:
        const auto offset            = current_dentry_index * sizeof(DirectoryEntry);
        const auto offset_in_cluster = offset % bytes_per_cluster;

        auto dentry = DirectoryEntry();
        if(const auto e = copy_cluster(current_cluster, offset_in_cluster, sizeof(DirectoryEntry), &dentry, false)) {
            return e;
        }

        do {
            if(dentry.name[0] == 0xE5) {
                break;
            }
            if(dentry.name[0] == 0x00) {
                return Error::Code::EndOfFile;
            }
            const auto target = current_index == index;
            if(dentry.attr & Attribute::LongName) {
                if(!target) {
                    break;
                }

                const auto& nentry = *std::bit_cast<LFNEntry*>(&dentry);
                if(nentry.number & 0x40) {
                    lfn_checksum = nentry.checksum;
                } else if(nentry.checksum != lfn_checksum) {
                    return Error::Code::BadChecksum;
                }
                lfn = nentry.to_string() + lfn;
                break;
            }

            current_index += 1;
            if(!target) {
                break;
            }

            result = DirectoryInfo{
                .cluster   = (uint32_t(dentry.first_cluster_high) << 16) | dentry.first_cluster_low,
                .size      = dentry.file_size,
                .name      = lfn_checksum == dentry.calc_checksum() ? u16tou8(lfn) : dentry.to_string(),
                .attribute = dentry.attr,
            };
            break;
        } while(0);

        current_dentry_index += 1;

        if(current_dentry_index % (bytes_per_cluster / sizeof(DirectoryEntry)) == 0) {
            auto handle_data = HandleData{
                .current_cluster        = current_cluster,
                .current_cluster_number = 0};
            if(const auto e = seek_cluster_chain(data, handle_data, 1)) {
                if(result && e == Error::Code::EndOfFile) {
                    current_cluster = eof_cluster;
                } else {
                    return e;
                }
            } else {
                current_cluster = handle_data.current_cluster;
            }
        }

        if(result) {
            handle.current_cluster = current_cluster;
            handle.current_index   = current_index;
            handle.dentry_index    = current_dentry_index;
            return result.value();
        }
        goto loop;
    }

  public:
    auto init(Handle device) -> Error {
        this->device = std::move(device);

        auto buffer = std::array<std::byte, sizeof(BPB)>();

        if(const auto e = device_io(0, sizeof(BPB), &buffer, false)) {
            return e;
        }

        const auto& bpb = *std::bit_cast<BPB*>(buffer.data());

        if(bpb.signature[0] != 0x55 || bpb.signature[1] != 0xAA) {
            return Error::Code::NotFAT;
        }

        const auto bytes_per_cluster = bpb.bytes_per_sector * bpb.sectors_per_cluster;
        if(paging::bytes_per_page >= bytes_per_cluster) {
            if(paging::bytes_per_page % bytes_per_cluster != 0) {
                return Error::Code::NotSupported;
            }
        } else {
            if(bytes_per_cluster % paging::bytes_per_page != 0) {
                return Error::Code::NotSupported;
            }
        }

        const auto root_data = FOPData{.type = FileType::Directory, .cluster = bpb.root_cluster, .filesize = 0};
        this->bpb            = bpb.summary();
        this->root.emplace(FileAbstractWithDriverData{{"/", 0, FileType::Directory, 0, fs::volume_root_attributes}, std::bit_cast<uint64_t>(root_data)});

        return Success();
    }

    auto read(const uint64_t fop_data, uint64_t& handle_data, const size_t block, const size_t count, void* const buffer) -> Result<size_t> override {
        const auto data   = std::bit_cast<FOPData>(fop_data);
        auto       handle = std::bit_cast<HandleData>(handle_data);

        const auto blocksize = size_t(bpb.bytes_per_sector) * bpb.sectors_per_cluster;
        for(auto i = size_t(0); i < count; i += 1) {
            if(const auto e = seek_cluster_chain(data, handle, block + i)) {
                return e;
            }

            if(const auto e = copy_cluster(handle.current_cluster, 0, blocksize, static_cast<std::byte*>(buffer) + i * blocksize, false)) {
                return e;
            }
        }

        handle_data = std::bit_cast<uint64_t>(handle);
        return count;
    }

    auto find(const uint64_t fop_data, uint64_t& handle_data, const std::string_view name) -> Result<FileAbstractWithDriverData> override {
        const auto data = std::bit_cast<FOPData>(fop_data);

        if(data.type != FileType::Directory) {
            return Error::Code::NotDirectory;
        }

        auto handle = HandleDirectoryData::from_fop_data(data);
        for(auto i = uint32_t(0);; i += 1) {
            const auto dinfo_r = readdir(data, handle, i);
            if(!dinfo_r) {
                return dinfo_r.as_error();
            }
            const auto& dinfo = dinfo_r.as_value();

            if(dinfo.name == name) {
                return file_abstract_from_dinfo(dinfo);
            }
        }
    }

    auto create(const uint64_t fop_data, uint64_t& handle_data, const std::string_view name, const FileType type) -> Result<FileAbstractWithDriverData> override {
        return Error::Code::NotSupported;
    }

    auto readdir(const uint64_t fop_data, uint64_t& handle_data, const size_t index) -> Result<FileAbstractWithDriverData> override {
        const auto data   = std::bit_cast<FOPData>(fop_data);
        auto       handle = std::bit_cast<HandleDirectoryData>(handle_data);

        if(data.type != FileType::Directory) {
            return Error::Code::NotDirectory;
        }

        const auto dinfo_r = readdir(data, handle, index);
        if(!dinfo_r) {
            return dinfo_r.as_error();
        }
        const auto& dinfo = dinfo_r.as_value();

        handle_data = std::bit_cast<uint64_t>(handle);
        return file_abstract_from_dinfo(dinfo);
    }

    auto remove(const uint64_t fop_data, uint64_t& handle_data, const std::string_view name) -> Error override {
        return Error::Code::NotSupported;
    }

    auto create_handle_data(const uint64_t fop_data) -> Result<uint64_t> override {
        const auto data = std::bit_cast<FOPData>(fop_data);

        switch(data.type) {
        case FileType::Regular:
            return std::bit_cast<uint64_t>(HandleData::from_fop_data(data));
        case FileType::Directory:
            return std::bit_cast<uint64_t>(HandleDirectoryData::from_fop_data(data));
        default:
            return Error::Code::InvalidData;
        }
    }

    auto get_root() -> FileAbstractWithDriverData& override {
        return root.value();
    }
};
} // namespace fs::fat
