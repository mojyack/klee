#pragma once
#include <functional>
#include <string>
#include <vector>

namespace commands {
inline auto list_blocks = std::function<std::vector<std::string>()>();
}
