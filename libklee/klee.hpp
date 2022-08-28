#pragma once
#include <string_view>

#include "error.hpp"

namespace klee {
class FileHandle {
  private:
    size_t number;

  public:
    auto get_filesize() -> size_t;
    auto read(size_t offset, size_t size, void* buffer) -> Result<size_t>;
    auto write(size_t offset, size_t size, const void* buffer) -> Result<size_t>;
};

auto open_handle(std::string_view path) -> Result<FileHandle>;
} // namespace klee
