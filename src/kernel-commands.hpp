#pragma once
#include <functional>
#include <string>
#include <vector>

#include "error.hpp"
#include "fs/control.hpp"

namespace commands {
struct MountRecord {
    std::string device;
    std::string path;
};

inline auto list_blocks         = std::function<std::vector<std::string>()>();
inline auto mount               = std::function<Error(std::string_view device, std::string_view path)>();
inline auto unmount             = std::function<Error(std::string_view path)>();
inline auto get_mounts          = std::function<std::vector<MountRecord>()>();
inline auto get_filesystem_root = std::function<fs::Controller&()>();
} // namespace commands
