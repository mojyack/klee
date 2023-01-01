#pragma once
#include <array>

#include "../lapic.hpp"

namespace smp {
inline auto default_lapic_id_to_index_table = std::array<size_t, 1>{0};
inline auto lapic_id_to_index_table         = default_lapic_id_to_index_table.data();

inline auto get_processor_number() -> size_t {
    return lapic_id_to_index_table[lapic::read_lapic_id()];
}
} // namespace smp
