#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "crc32_iso_hdlc.h"
#include "protocol_version.h"
#include "register_endian.h"

static void test_crc_check_value(void)
{
    static const uint8_t input[] = "123456789";
    assert(rover_crc32_iso_hdlc(input, sizeof(input) - 1) ==
           UINT32_C(0xCBF43926));
}

static void test_u32_high_word_first(void)
{
    uint16_t registers[2] = {0};
    rover_register_write_u32(registers, 0, UINT32_C(0x1234ABCD));
    assert(registers[0] == UINT16_C(0x1234));
    assert(registers[1] == UINT16_C(0xABCD));
    assert(rover_register_read_u32(registers, 0) == UINT32_C(0x1234ABCD));
}

static void test_register_byte_order_and_crc(void)
{
    const uint16_t registers[] = {UINT16_C(0x3132), UINT16_C(0x3334),
                                  UINT16_C(0x3536), UINT16_C(0x3738),
                                  UINT16_C(0x3900)};
    uint8_t bytes[sizeof(registers)] = {0};
    static const uint8_t expected[] = {'1', '2', '3', '4', '5',
                                       '6', '7', '8', '9', 0};

    rover_registers_to_be_bytes(registers, 5, bytes);
    assert(memcmp(bytes, expected, sizeof(expected)) == 0);
    assert(rover_crc32_registers_be(registers, 4) ==
           rover_crc32_iso_hdlc(expected, 8));
}

int main(void)
{
    test_crc_check_value();
    test_u32_high_word_first();
    test_register_byte_order_and_crc();
    puts("core tests passed");
    return 0;
}
