#pragma once
#include <bit>
#include <cstdint>

namespace lapic {
constexpr auto lapic_base = 0xFE'E0'00'00lu;

struct LAPICRegisters {
    alignas(16) uint32_t reserved1_1;                           // +0x0000
    alignas(16) uint32_t reserved1_2;                           // +0x0010
    alignas(16) uint32_t lapic_id;                              // +0x0020 (rw)
    alignas(16) uint32_t lapic_version;                         // +0x0030 (ro)
    alignas(16) uint32_t reserved2_1;                           // +0x0040
    alignas(16) uint32_t reserved2_2;                           // +0x0050
    alignas(16) uint32_t reserved2_3;                           // +0x0060
    alignas(16) uint32_t reserved2_4;                           // +0x0070
    alignas(16) uint32_t task_priority;                         // +0x0080 (rw)
    alignas(16) uint32_t arbitration_priority;                  // +0x0090 (ro)
    alignas(16) uint32_t processor_priority;                    // +0x00A0 (ro)
    alignas(16) uint32_t end_of_interrupt;                      // +0x00B0 (wo)
    alignas(16) uint32_t remote_read;                           // +0x00C0 (ro)
    alignas(16) uint32_t logical_destination;                   // +0x00D0 (rw)
    alignas(16) uint32_t destination_format;                    // +0x00E0 (rw)
    alignas(16) uint32_t spurious_interrupt_vector;             // +0x00F0 (rw)
    alignas(16) uint32_t in_service_0;                          // +0x0100 (ro)
    alignas(16) uint32_t in_service_1;                          // +0x0110 (ro)
    alignas(16) uint32_t in_service_2;                          // +0x0120 (ro)
    alignas(16) uint32_t in_service_3;                          // +0x0130 (ro)
    alignas(16) uint32_t in_service_4;                          // +0x0140 (ro)
    alignas(16) uint32_t in_service_5;                          // +0x0150 (ro)
    alignas(16) uint32_t in_service_6;                          // +0x0160 (ro)
    alignas(16) uint32_t in_service_7;                          // +0x0170 (ro)
    alignas(16) uint32_t trigger_mode_0;                        // +0x0180 (ro)
    alignas(16) uint32_t trigger_mode_1;                        // +0x0190 (ro)
    alignas(16) uint32_t trigger_mode_2;                        // +0x01A0 (ro)
    alignas(16) uint32_t trigger_mode_3;                        // +0x01B0 (ro)
    alignas(16) uint32_t trigger_mode_4;                        // +0x01C0 (ro)
    alignas(16) uint32_t trigger_mode_5;                        // +0x01D0 (ro)
    alignas(16) uint32_t trigger_mode_6;                        // +0x01E0 (ro)
    alignas(16) uint32_t trigger_mode_7;                        // +0x01F0 (ro)
    alignas(16) uint32_t interrupt_request_0;                   // +0x0200 (ro)
    alignas(16) uint32_t interrupt_request_1;                   // +0x0210 (ro)
    alignas(16) uint32_t interrupt_request_2;                   // +0x0220 (ro)
    alignas(16) uint32_t interrupt_request_3;                   // +0x0230 (ro)
    alignas(16) uint32_t interrupt_request_4;                   // +0x0240 (ro)
    alignas(16) uint32_t interrupt_request_5;                   // +0x0250 (ro)
    alignas(16) uint32_t interrupt_request_6;                   // +0x0260 (ro)
    alignas(16) uint32_t interrupt_request_7;                   // +0x0270 (ro)
    alignas(16) uint32_t error_status;                          // +0x0280 (ro)
    alignas(16) uint32_t reserved3_1;                           // +0x0290
    alignas(16) uint32_t reserved3_2;                           // +0x02A0
    alignas(16) uint32_t reserved3_3;                           // +0x02B0
    alignas(16) uint32_t reserved3_4;                           // +0x02C0
    alignas(16) uint32_t reserved3_5;                           // +0x02D0
    alignas(16) uint32_t reserved3_6;                           // +0x02E0
    alignas(16) uint32_t lvt_corrected_machine_check_interrupt; // +0x02F0 (rw)
    alignas(16) uint32_t interrupt_command_0;                   // +0x0300 (rw)
    alignas(16) uint32_t interrupt_command_1;                   // +0x0310 (rw)
    alignas(16) uint32_t lvt_timer;                             // +0x0320 (rw)
    alignas(16) uint32_t lvt_thermal_sensor;                    // +0x0330 (rw)
    alignas(16) uint32_t lvt_performance_monitoring_counters;   // +0x0340 (rw)
    alignas(16) uint32_t lvt_lint_0;                            // +0x0350 (rw)
    alignas(16) uint32_t lvt_lint_1;                            // +0x0360 (rw)
    alignas(16) uint32_t lvt_error;                             // +0x0370 (rw)
    alignas(16) uint32_t initial_count;                         // +0x0380 (rw)
    alignas(16) uint32_t current_count;                         // +0x0390 (ro)
    alignas(16) uint32_t reserved4_1;                           // +0x03A0
    alignas(16) uint32_t reserved4_2;                           // +0x03B0
    alignas(16) uint32_t reserved4_3;                           // +0x03C0
    alignas(16) uint32_t reserved4_4;                           // +0x03D0
    alignas(16) uint32_t divide_configuration;                  // +0x03E0 (rw)
    alignas(16) uint32_t reserved5_1;                           // +0x03F0
} __attribute__((packed));

static_assert(sizeof(LAPICRegisters) == 0x400);

inline auto get_lapic_registers() -> volatile LAPICRegisters& {
    return *(std::bit_cast<LAPICRegisters*>(lapic_base));
}

inline auto read_lapic_id() -> uint8_t {
    return get_lapic_registers().lapic_id >> 24;
}
} // namespace lapic
