#pragma once
#include <stdint.h>

extern "C" {
void     io_set32(uint16_t address, uint32_t data);
uint32_t io_read32(uint16_t address);
uint16_t read_cs(void); // read code segment register
void     load_idt(uint16_t limit, uint64_t offset);
}
