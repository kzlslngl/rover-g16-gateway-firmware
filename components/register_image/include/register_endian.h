#ifndef ROVER_G16_REGISTER_ENDIAN_H
#define ROVER_G16_REGISTER_ENDIAN_H

#include <stddef.h>
#include <stdint.h>

void rover_register_write_u32(uint16_t *registers, size_t offset,
                              uint32_t value);
uint32_t rover_register_read_u32(const uint16_t *registers, size_t offset);
void rover_registers_to_be_bytes(const uint16_t *registers, size_t count,
                                 uint8_t *bytes);

#endif
