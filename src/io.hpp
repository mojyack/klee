#pragma once
#include "asmcode.hpp"

namespace io::internal {
constexpr auto config_address = 0x0CF8;
constexpr auto config_data    = 0x0CFC;
} // namespace io::internal

inline auto write_address(const uint32_t address) -> void {
    io_set32(io::internal::config_address, address);
}

inline auto write_data(const uint32_t data) -> void {
    io_set32(io::internal::config_data, data);
}

inline auto read_data() -> uint32_t {
    return io_read32(io::internal::config_data);
}

inline auto read_data(const uint32_t address) -> uint32_t {
    write_address(address);
    return io_read32(io::internal::config_data);
}
