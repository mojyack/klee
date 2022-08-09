#pragma once
#include <array>
#include <cstdio>
#include <memory>
#include <vector>

#include "../encoding.hpp"
#include "drivers/partition.hpp"

namespace block::gpt {
struct MBR {
    struct PartitionTable {
        uint8_t bootable;
        uint8_t first_sector[3];
        uint8_t type;
        uint8_t last_sector[3];
        uint8_t first_lba_sector[4];
        uint8_t num_sectors[4];
    } __attribute__((packed));

    uint8_t        loader[446];
    PartitionTable partition[4];
    uint8_t        signature[2];
} __attribute__((packed));

static_assert(sizeof(MBR) == 512);

struct GUID {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];

    auto to_string() const -> std::array<char, 37> {
        auto buf = std::array<char, 32 + 4 + 1>();
        sprintf(buf.data(), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", data1, data2, data3, data4[0], data4[1], data4[2], data4[3], data4[4], data4[5], data4[6], data4[7]);
        return buf;
    }

    auto operator==(const GUID& o) const -> bool {
        return data1 == o.data1 && data2 == o.data2 && data3 == o.data3 && memcmp(data4, o.data4, 8) == 0;
    }
} __attribute__((packed));

namespace partition_type {
constexpr auto esp = GUID{0xC12A7328, 0xF81F, 0x11D2, {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}};
}

struct PartitionTableHeader {
    char     signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t gpt_header_checksum;
    uint32_t reserved1;
    uint64_t lba_self;
    uint64_t lba_alt;
    uint64_t first_usable;
    uint64_t last_usable;
    GUID     disk_guid;
    uint64_t entry_array_lba;
    uint32_t num_entries;
    uint32_t entry_size;
    uint32_t entry_array_checksum;
} __attribute__((packed));

struct PartitionEntry {
    GUID     type;
    GUID     id;
    uint64_t lba_start;
    uint64_t lba_last;
    uint64_t attribute;
    char16_t name[36];

    auto get_u8name() const -> std::array<char, 36> {
        auto buffer = std::array<char, 36>();
        auto u8     = u16tou8(name);
        std::memcpy(buffer.data(), u8.data(), buffer.size());
        return buffer;
    }
} __attribute__((packed));

enum class Filesystem {
    Unknown,
    FAT32,
};

struct Partition {
    Filesystem                   filesystem;
    std::unique_ptr<BlockDevice> device;
};

inline auto find_partitions(BlockDevice* const device) -> Result<std::vector<Partition>> {
    auto info   = device->get_info();
    auto buffer = std::vector<uint8_t>(info.bytes_per_sector);

    device->read_sector(0, 1, buffer.data());
    const auto& mbr = *reinterpret_cast<MBR*>(buffer.data());
    if(mbr.signature[0] != 0x55 || mbr.signature[1] != 0xAA) {
        return Error::Code::NotMBR;
    }

    if(mbr.partition[0].type != 0xEE) {
        return Error::Code::NotGPT;
    }

    device->read_sector(1, 1, buffer.data());
    const auto header = *reinterpret_cast<PartitionTableHeader*>(buffer.data());
    if(std::string_view(header.signature, 8) != "EFI PART") {
        return Error::Code::NotGPT;
    }
    if(header.entry_size != 128) {
        return Error::Code::UnsupportedGPT;
    }

    auto result = std::vector<Partition>();
    for(auto i = 0, buffer_lba = -1; i < header.num_entries; i += 1) {
        const auto lba = header.entry_array_lba + sizeof(PartitionEntry) * i / info.bytes_per_sector;
        if(buffer_lba != lba) {
            buffer_lba = lba;
            device->read_sector(lba, 1, buffer.data());
        }
        const auto& entry = *reinterpret_cast<PartitionEntry*>(buffer.data() + sizeof(PartitionEntry) * i % info.bytes_per_sector);
        if(entry.type == GUID{0, 0}) {
            continue;
        }
        auto fs = Filesystem::Unknown;
        if(entry.type == partition_type::esp) {
            fs = Filesystem::FAT32;
        }
        auto dev = new block::partition::PartitionBlockDevice(device, entry.lba_start, entry.lba_last - entry.lba_start + 1);
        result.emplace_back(Partition{fs, std::unique_ptr<block::partition::PartitionBlockDevice>(dev)});
    }
    return result;
}
} // namespace block::gpt
