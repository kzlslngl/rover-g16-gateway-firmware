#include "crc32_iso_hdlc.h"

#define CRC32_POLYNOMIAL_REFLECTED UINT32_C(0xEDB88320)

static uint32_t crc32_update_byte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit) {
        const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
        crc = (crc >> 1) ^ (CRC32_POLYNOMIAL_REFLECTED & mask);
    }
    return crc;
}

uint32_t rover_crc32_iso_hdlc(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < length; ++index) {
        crc = crc32_update_byte(crc, data[index]);
    }
    return crc ^ UINT32_MAX;
}

uint32_t rover_crc32_registers_be(const uint16_t *registers, size_t count)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < count; ++index) {
        crc = crc32_update_byte(crc, (uint8_t)(registers[index] >> 8));
        crc = crc32_update_byte(crc, (uint8_t)registers[index]);
    }
    return crc ^ UINT32_MAX;
}
