#pragma once
#include <cstdint>

extern "C" {
auto io_set32(uint16_t address, uint32_t data) -> void;
auto io_read32(uint16_t address) -> uint32_t;
auto read_cs(void) -> uint16_t;
auto load_idt(uint16_t limit, uint64_t offset) -> void;
auto load_gdt(uint16_t limit, uint64_t offset) -> void;
auto set_csss(uint16_t cs, uint16_t ss) -> void;
auto set_dsall(uint16_t value) -> void;
auto write_msr(uint32_t msr, uint64_t value) -> void;
auto jump_to_app(uint64_t id, int64_t data, uint16_t ss, uint64_t rip, uint64_t rsp, uint64_t* system_stack_ptr) -> int;
auto syscall_entry() -> void;
auto load_tr(uint16_t sel) -> void;
auto int_handler_lapic_timer_entry() -> void;
}
