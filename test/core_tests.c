#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "crc32_iso_hdlc.h"
#include "freshness.h"
#include "protocol_version.h"
#include "register_endian.h"
#include "register_image.h"
#include "sbus_decoder.h"
#include "session_id.h"

static const uint8_t known_sbus_frame[ROVER_G16_SBUS_FRAME_SIZE] = {
    0x0F, 0x00, 0x08, 0x00, 0x2B, 0xC0, 0x37, 0xF1, 0xFF,
    0x93, 0x01, 0x19, 0x2C, 0x81, 0x0C, 0x7D, 0xB0, 0xC4,
    0x2B, 0x90, 0x11, 0x0E, 0x7D, 0x00, 0x00,
};

static const uint16_t known_sbus_channels[ROVER_G16_CHANNEL_COUNT] = {
    0, 1, 172, 992, 1811, 2047, 100, 200,
    300, 400, 500, 600, 700, 800, 900, 1000,
};

static const struct rover_sbus_config test_sbus_config = {
    .allowed_footers = {0x00},
    .allowed_footer_count = 1,
};

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

static void test_sbus_known_frame(void)
{
    struct rover_sbus_frame decoded = {0};
    assert(rover_sbus_decode(known_sbus_frame, sizeof(known_sbus_frame),
                             &test_sbus_config, &decoded) ==
           ROVER_SBUS_DECODE_OK);
    assert(memcmp(decoded.channels, known_sbus_channels,
                  sizeof(known_sbus_channels)) == 0);
    assert(!decoded.digital_channel_17);
    assert(!decoded.digital_channel_18);
    assert(!decoded.frame_lost);
    assert(!decoded.receiver_failsafe);
}

static void test_sbus_flags(void)
{
    uint8_t bytes[ROVER_G16_SBUS_FRAME_SIZE];
    struct rover_sbus_frame decoded = {0};
    memcpy(bytes, known_sbus_frame, sizeof(bytes));
    bytes[23] = 0x0Fu;

    assert(rover_sbus_decode(bytes, sizeof(bytes), &test_sbus_config,
                             &decoded) == ROVER_SBUS_DECODE_OK);
    assert(decoded.digital_channel_17);
    assert(decoded.digital_channel_18);
    assert(decoded.frame_lost);
    assert(decoded.receiver_failsafe);
}

static void test_sbus_rejects_bad_structure(void)
{
    uint8_t bytes[ROVER_G16_SBUS_FRAME_SIZE];
    struct rover_sbus_frame decoded = {0};
    memcpy(bytes, known_sbus_frame, sizeof(bytes));

    assert(rover_sbus_decode(bytes, sizeof(bytes) - 1, &test_sbus_config,
                             &decoded) == ROVER_SBUS_DECODE_BAD_LENGTH);
    bytes[0] = 0x55u;
    assert(rover_sbus_decode(bytes, sizeof(bytes), &test_sbus_config,
                             &decoded) == ROVER_SBUS_DECODE_BAD_HEADER);
    bytes[0] = ROVER_SBUS_HEADER;
    bytes[24] = 0x55u;
    assert(rover_sbus_decode(bytes, sizeof(bytes), &test_sbus_config,
                             &decoded) == ROVER_SBUS_DECODE_BAD_FOOTER);
}

static void test_sbus_parser_noise_and_gap(void)
{
    struct rover_sbus_parser parser;
    struct rover_sbus_frame decoded = {0};
    assert(rover_sbus_parser_init(&parser, &test_sbus_config));

    assert(rover_sbus_parser_push(&parser, 0xAAu, &decoded) ==
           ROVER_SBUS_PARSER_NONE);
    for (size_t index = 0; index < 10; ++index) {
        assert(rover_sbus_parser_push(&parser, known_sbus_frame[index],
                                      &decoded) == ROVER_SBUS_PARSER_NONE);
    }
    assert(rover_sbus_parser_on_gap(&parser));
    assert(!rover_sbus_parser_on_gap(&parser));

    for (size_t index = 0; index < sizeof(known_sbus_frame); ++index) {
        const enum rover_sbus_parser_event expected =
            index + 1 == sizeof(known_sbus_frame) ? ROVER_SBUS_PARSER_FRAME
                                                  : ROVER_SBUS_PARSER_NONE;
        assert(rover_sbus_parser_push(&parser, known_sbus_frame[index],
                                      &decoded) == expected);
    }
    assert(memcmp(decoded.channels, known_sbus_channels,
                  sizeof(known_sbus_channels)) == 0);
}

static void test_sbus_parser_resyncs_to_embedded_header(void)
{
    struct rover_sbus_parser parser;
    struct rover_sbus_frame decoded = {0};
    assert(rover_sbus_parser_init(&parser, &test_sbus_config));

    assert(rover_sbus_parser_push(&parser, ROVER_SBUS_HEADER, &decoded) ==
           ROVER_SBUS_PARSER_NONE);
    for (size_t index = 0; index < 23; ++index) {
        assert(rover_sbus_parser_push(&parser, known_sbus_frame[index],
                                      &decoded) == ROVER_SBUS_PARSER_NONE);
    }
    assert(rover_sbus_parser_push(&parser, 0x55u, &decoded) ==
           ROVER_SBUS_PARSER_REJECTED);

    assert(parser.count == 24);
    assert(rover_sbus_parser_push(&parser, known_sbus_frame[24], &decoded) ==
           ROVER_SBUS_PARSER_FRAME);
    assert(memcmp(decoded.channels, known_sbus_channels,
                  sizeof(known_sbus_channels)) == 0);
}

static struct rover_sbus_frame make_usable_frame(void)
{
    struct rover_sbus_frame frame = {0};
    memcpy(frame.channels, known_sbus_channels, sizeof(frame.channels));
    return frame;
}

static void test_freshness_boot_and_valid_frame(void)
{
    struct rover_freshness_state state;
    struct rover_freshness_view view;
    struct rover_sbus_frame frame = make_usable_frame();
    rover_freshness_init(&state);
    rover_freshness_set_alive(&state, true);

    rover_freshness_make_view(&state, UINT64_C(1000000), 100, &view);
    assert(view.frame_age_ms == UINT16_MAX);
    assert(view.channel_valid_mask == 0);
    assert(view.sbus_flags == ROVER_G16_SBUS_FLAG_ALIVE);

    assert(rover_freshness_record_frame(&state, &frame, UINT64_C(1000000)));
    rover_freshness_make_view(&state, UINT64_C(1025000), 100, &view);
    assert(view.frame_age_ms == 25);
    assert(view.channel_valid_mask == UINT16_MAX);
    assert((view.sbus_flags & ROVER_G16_SBUS_FLAG_VALID) != 0);
    assert(view.sbus_frame_counter == 1);
    assert(memcmp(view.channels, known_sbus_channels,
                  sizeof(known_sbus_channels)) == 0);
}

static void test_freshness_stale_lost_failsafe_and_fault(void)
{
    struct rover_freshness_state state;
    struct rover_freshness_view view;
    struct rover_sbus_frame frame = make_usable_frame();
    rover_freshness_init(&state);
    rover_freshness_set_alive(&state, true);
    assert(rover_freshness_record_frame(&state, &frame, UINT64_C(1000000)));
    assert(rover_freshness_record_frame(&state, &frame, UINT64_C(1014000)));
    assert(state.frame_period_us == 14000);

    rover_freshness_make_view(&state, UINT64_C(1200000), 100, &view);
    assert(view.frame_age_ms == 186);
    assert((view.sbus_flags & ROVER_G16_SBUS_FLAG_VALID) == 0);
    assert(view.channel_valid_mask == 0);

    frame.frame_lost = true;
    frame.receiver_failsafe = true;
    assert(!rover_freshness_record_frame(&state, &frame, UINT64_C(1210000)));
    assert(state.sbus_frame_counter == 2);
    assert(state.frame_lost_count == 1);
    assert(state.failsafe_count == 1);
    rover_freshness_make_view(&state, UINT64_C(1210000), 1000, &view);
    assert((view.sbus_flags & ROVER_G16_SBUS_FLAG_FRAME_LOST) != 0);
    assert((view.sbus_flags & ROVER_G16_SBUS_FLAG_FAILSAFE) != 0);
    assert(view.channel_valid_mask == 0);

    frame.frame_lost = false;
    frame.receiver_failsafe = false;
    assert(rover_freshness_record_frame(&state, &frame, UINT64_C(1220000)));
    rover_freshness_set_fault(&state, true);
    rover_freshness_make_view(&state, UINT64_C(1220000), 100, &view);
    assert((view.sbus_flags & ROVER_G16_SBUS_FLAG_FAULT) != 0);
    assert((view.sbus_flags & ROVER_G16_SBUS_FLAG_VALID) == 0);
}

static void test_freshness_saturation_and_counter_wrap(void)
{
    struct rover_freshness_state state;
    struct rover_freshness_view view;
    struct rover_sbus_frame frame = make_usable_frame();
    rover_freshness_init(&state);
    assert(rover_freshness_record_frame(&state, &frame, 0));
    rover_freshness_make_view(&state, UINT64_C(70000000), UINT32_MAX, &view);
    assert(view.frame_age_ms == UINT16_MAX);

    state.invalid_frame_count = UINT32_MAX;
    rover_freshness_record_invalid(&state);
    assert(state.invalid_frame_count == 0);
}

static void test_freshness_averages_batched_frame_timestamps(void)
{
    struct rover_freshness_state state;
    struct rover_sbus_frame frame = make_usable_frame();
    rover_freshness_init(&state);

    assert(rover_freshness_record_frame(&state, &frame, 1000000));
    assert(rover_freshness_record_frame(&state, &frame, 1000065));
    assert(rover_freshness_record_frame(&state, &frame, 1000130));
    assert(rover_freshness_record_frame(&state, &frame, 1000195));
    assert(state.frame_period_us == 0);
    assert(rover_freshness_record_frame(&state, &frame, 1049000));
    assert(state.frame_period_us == 12250);
}

static void test_freshness_rejects_scheduler_delay_outlier(void)
{
    struct rover_freshness_state state;
    struct rover_sbus_frame frame = make_usable_frame();
    rover_freshness_init(&state);

    assert(rover_freshness_record_frame(&state, &frame, 1000000));
    assert(rover_freshness_record_frame(&state, &frame, 1010000));
    assert(state.frame_period_us == 10000);
    assert(rover_freshness_record_frame(&state, &frame, 1060000));
    assert(state.frame_period_us == 10000);
    assert(rover_freshness_record_frame(&state, &frame, 1085000));
    assert(state.frame_period_us == 10000);
    assert(rover_freshness_record_frame(&state, &frame, 1130000));
    assert(state.frame_period_us == 10000);
}

static void test_register_image_boot_and_crc(void)
{
    struct rover_register_image_state image_state;
    struct rover_freshness_state freshness_state;
    struct rover_freshness_view view;
    uint16_t registers[ROVER_G16_REGISTER_COUNT];
    rover_freshness_init(&freshness_state);
    rover_freshness_make_view(&freshness_state, 0, 100, &view);
    assert(rover_register_image_init(&image_state, UINT32_C(0x12345678)));
    assert(rover_register_image_build(&image_state, &view, 42, registers));

    assert(registers[ROVER_G16_REG_MAGIC] == ROVER_G16_PROTOCOL_MAGIC);
    assert(registers[ROVER_G16_REG_VERSION_MAJOR] == 1);
    assert(registers[ROVER_G16_REG_VERSION_MINOR] == 1);
    assert(registers[ROVER_G16_REG_LENGTH] == ROVER_G16_REGISTER_COUNT);
    assert(rover_register_read_u32(registers,
                                   ROVER_G16_REG_BEGIN_SEQUENCE) == 1);
    assert(rover_register_read_u32(registers,
                                   ROVER_G16_REG_END_SEQUENCE) == 1);
    assert(rover_register_read_u32(registers, ROVER_G16_REG_SESSION_ID) ==
           UINT32_C(0x12345678));
    assert(registers[ROVER_G16_REG_FRAME_AGE_MS] == UINT16_MAX);
    assert(registers[ROVER_G16_REG_CHANNEL_VALID_MASK] == 0);
    for (size_t index = ROVER_G16_REG_RESERVED;
         index < ROVER_G16_REG_CRC32; ++index) {
        assert(registers[index] == 0);
    }
    const uint32_t expected_crc = rover_crc32_registers_be(
        registers, ROVER_G16_CRC_INPUT_REGISTER_COUNT);
    assert(rover_register_read_u32(registers, ROVER_G16_REG_CRC32) ==
           expected_crc);
}

static void test_register_image_valid_channels_and_progress(void)
{
    struct rover_register_image_state image_state;
    struct rover_freshness_state freshness_state;
    struct rover_freshness_view view;
    struct rover_sbus_frame frame = make_usable_frame();
    uint16_t registers[ROVER_G16_REGISTER_COUNT];
    rover_freshness_init(&freshness_state);
    rover_freshness_set_alive(&freshness_state, true);
    assert(rover_freshness_record_frame(&freshness_state, &frame, 1000));
    rover_freshness_make_view(&freshness_state, 2000, 100, &view);
    assert(rover_register_image_init(&image_state, 7));
    assert(rover_register_image_build(&image_state, &view, 2, registers));
    assert(registers[ROVER_G16_REG_CHANNEL_VALID_MASK] == UINT16_MAX);
    assert(memcmp(&registers[ROVER_G16_REG_CHANNEL_RAW], known_sbus_channels,
                  sizeof(known_sbus_channels)) == 0);
    assert(rover_register_image_build(&image_state, &view, 3, registers));
    assert(rover_register_read_u32(registers,
                                   ROVER_G16_REG_BEGIN_SEQUENCE) == 2);
    assert(rover_register_read_u32(registers, ROVER_G16_REG_HEARTBEAT) == 2);
}

static void test_session_id_is_deterministic_and_input_sensitive(void)
{
    const uint64_t device_id = UINT64_C(0xAABBCCDDEEFF);
    const uint32_t session = rover_session_id_derive(
        device_id, UINT32_C(42), UINT32_C(0x12345678));
    assert(session != 0);
    assert(session == rover_session_id_derive(
                          device_id, UINT32_C(42), UINT32_C(0x12345678)));
    assert(session != rover_session_id_derive(
                          device_id, UINT32_C(43), UINT32_C(0x12345678)));
    assert(session != rover_session_id_derive(
                          device_id, UINT32_C(42), UINT32_C(0x12345679)));
    assert(session != rover_session_id_derive(
                          device_id + 1, UINT32_C(42), UINT32_C(0x12345678)));
}

int main(void)
{
    test_crc_check_value();
    test_u32_high_word_first();
    test_register_byte_order_and_crc();
    test_sbus_known_frame();
    test_sbus_flags();
    test_sbus_rejects_bad_structure();
    test_sbus_parser_noise_and_gap();
    test_sbus_parser_resyncs_to_embedded_header();
    test_freshness_boot_and_valid_frame();
    test_freshness_stale_lost_failsafe_and_fault();
    test_freshness_saturation_and_counter_wrap();
    test_freshness_averages_batched_frame_timestamps();
    test_freshness_rejects_scheduler_delay_outlier();
    test_register_image_boot_and_crc();
    test_register_image_valid_channels_and_progress();
    test_session_id_is_deterministic_and_input_sensitive();
    puts("core tests passed");
    return 0;
}
