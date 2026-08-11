#include "register_endian.h"

void rover_register_write_u32(uint16_t *registers, size_t offset,
                              uint32_t value)
{
    registers[offset] = (uint16_t)(value >> 16);
    registers[offset + 1] = (uint16_t)value;
}

uint32_t rover_register_read_u32(const uint16_t *registers, size_t offset)
{
    return ((uint32_t)registers[offset] << 16) | registers[offset + 1];
}

void rover_registers_to_be_bytes(const uint16_t *registers, size_t count,
                                 uint8_t *bytes)
{
    for (size_t index = 0; index < count; ++index) {
        bytes[index * 2] = (uint8_t)(registers[index] >> 8);
        bytes[index * 2 + 1] = (uint8_t)registers[index];
    }
}
