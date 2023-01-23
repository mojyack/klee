#pragma once
#include <stdint.h>

extern "C" {
void     io_set32(uint16_t address, uint32_t data);
uint32_t io_read32(uint16_t address);
uint16_t read_cs(void); // read code segment register
void     load_idt(uint16_t limit, uint64_t offset);
void     load_gdt(uint16_t limit, uint64_t offset);
void     set_csss(uint16_t cs, uint16_t ss);
void     set_dsall(uint16_t value);
void     write_msr(uint32_t msr, uint64_t value);
int      jump_to_app(uint64_t id, int64_t data, uint16_t ss, uint64_t rip, uint64_t rsp, uint64_t* system_stack_ptr);
void     syscall_entry();
void     load_tr(uint16_t sel);
void     int_handler_lapic_timer_entry();
}
