#ifndef ROVER_G16_CRC32_ISO_HDLC_H
#define ROVER_G16_CRC32_ISO_HDLC_H

#include <stddef.h>
#include <stdint.h>

uint32_t rover_crc32_iso_hdlc(const uint8_t *data, size_t length);
uint32_t rover_crc32_registers_be(const uint16_t *registers, size_t count);

#endif
