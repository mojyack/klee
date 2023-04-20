#pragma once
#include <string>

namespace fs {
enum class OpenLevel : int {
    Block  = 0,
    Single = 1,
    Multi  = 2,
};

struct Attributes {
    OpenLevel read_level : 2;
    OpenLevel write_level : 2;
    bool      exclusive;
    bool      volume_root;
    bool      cache; // enable page cache
    bool      keep_on_close;
};

enum class FileType : uint32_t {
    Regular = 0,
    Directory,
    Device,
    FileTypeEnd,
};

using BlockSizeExp = uint8_t;

constexpr auto default_attributes = Attributes{
    .read_level    = OpenLevel::Single,
    .write_level   = OpenLevel::Single,
    .exclusive     = true,
    .volume_root   = false,
    .cache         = true,
    .keep_on_close = false,
};

constexpr auto volume_root_attributes = Attributes{
    .read_level    = OpenLevel::Single,
    .write_level   = OpenLevel::Single,
    .exclusive     = true,
    .volume_root   = true,
    .cache         = true,
    .keep_on_close = false,
};

struct FileAbstract {
    const std::string  name;
    size_t             filesize;
    const FileType     type;
    const BlockSizeExp blocksize_exp; // blocksize = 2^blocksize_exp
    const Attributes   attributes;
};
} // namespace fs
