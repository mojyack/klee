#include <string>

namespace fs::fat {
struct BPB {
    uint8_t  jump_boot[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  num_fats;
    uint16_t root_entry_count; // unused
    uint16_t total_sectors_16; // 0 unused
    uint8_t  media;
    uint16_t fat_size_16;       // 0 unused
    uint16_t sectors_per_track; // unused
    uint16_t num_heads;         // unused
    uint32_t hidden_sectors;    // unused
    uint32_t total_sectors_32;
    uint32_t fat_size_32; // sectors per fat
    struct {
        uint32_t active_fat : 4;
        uint32_t reserved1 : 3;
        uint32_t flag : 1; // 0: all fats are mirrored 1: only one fat is active (indicated in active_fat)
        uint32_t reserved2 : 8;
    } __attribute__((packed)) ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;   // unused
    uint8_t  reserved1;      // 0
    uint8_t  boot_signature; // ==0x29: following three fields are valid
    uint32_t volume_id;
    char     volume_label[11]; // match the volume label in the root directory
    char     fs_type[8];       // "FAT32   "
    uint8_t  loader[420];
    uint8_t  signature[2];

    struct Summary {
        uint16_t bytes_per_sector;
        uint8_t  sectors_per_cluster;
        uint16_t reserved_sector_count;
        uint8_t  num_fats;
        uint32_t total_sectors_32;
        uint32_t fat_size_32;
        uint32_t root_cluster;
    };

    auto summary() const -> Summary {
        return Summary{bytes_per_sector, sectors_per_cluster, reserved_sector_count, num_fats, total_sectors_32, fat_size_32, root_cluster};
    }
} __attribute__((packed));

static_assert(sizeof(BPB) == 512);

enum Attribute : uint8_t {
    ReadOnly  = 0x01,
    Hidden    = 0x02,
    System    = 0x04,
    VolumeID  = 0x08,
    Directory = 0x10,
    Archive   = 0x20,
    LongName  = 0x0F,
};

struct DirectoryEntry {
    unsigned char name[11];
    Attribute     attr;
    uint8_t       ntres;
    uint8_t       create_time_tenth;
    uint16_t      create_time;
    uint16_t      create_date;
    uint16_t      last_access_date;
    uint16_t      first_cluster_high;
    uint16_t      write_time;
    uint16_t      write_date;
    uint16_t      first_cluster_low;
    uint32_t      file_size;

    auto get_first_cluster() const -> uint32_t {
        return first_cluster_low | (static_cast<uint32_t>(first_cluster_high) << 16);
    }

    auto calc_checksum() const -> uint8_t {
        auto sum = uint8_t(0);
        for(auto i = 0; i < 11; i += 1) {
            sum = (sum >> 1) + (sum << 7) + name[i];
        }
        return sum;
    }

    auto to_string() const -> std::string {
        auto base = std::string();
        for(auto i = 0; i < 8; i += 1) {
            if(name[i] == 0x20) {
                break;
            }
            base += name[i];
        }
        auto ext = std::string();
        for(auto i = 8; i < 11; i += 1) {
            if(name[i] == 0x20) {
                break;
            }
            ext += name[i];
        }
        auto name = base;
        if(!ext.empty()) {
            name += "." + ext;
        }

        return name;
    }
} __attribute__((packed));

struct LFNEntry {
    uint8_t   number;
    char16_t  name1[5];
    Attribute attr;
    uint8_t   type; // 0
    uint8_t   checksum;
    char16_t  name2[6];
    uint16_t  first_cluster_low; // 0
    char16_t  name3[2];

    auto to_string() -> std::u16string {
        auto r = std::u16string();

        const auto helper = [&r](const int len, char16_t* const data) -> bool {
            for(auto i = 0; i < len; i += 1) {
                if(data[i] != u'\0') {
                    r += data[i];
                } else {
                    return true;
                }
            }
            return false;
        };

        if(helper(5, name1)) {
            return r;
        }
        if(helper(6, name2)) {
            return r;
        }
        if(helper(2, name3)) {
            return r;
        }
        return r;
    }
} __attribute__((packed));
} // namespace fs::fat
