#pragma once
#include <array>

#include "../lapic/registers.hpp"

namespace smp {
using ProcessorNumber = size_t;

constexpr auto invalid_processor_number = ProcessorNumber(-1);

inline auto first_lapic_id                  = uint8_t(0);
inline auto last_lapic_id                   = uint8_t(0);
inline auto default_lapic_id_to_index_table = std::array<ProcessorNumber, 1>{0};
inline auto lapic_id_to_index_table         = default_lapic_id_to_index_table.data();

inline auto get_processor_number() -> ProcessorNumber {
    return lapic_id_to_index_table[lapic::read_lapic_id()];
}
} // namespace smp
