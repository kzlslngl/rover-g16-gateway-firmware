#ifndef ROVER_G16_SBUS_DECODER_H
#define ROVER_G16_SBUS_DECODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol_version.h"

enum {
    ROVER_SBUS_HEADER = 0x0Fu,
    ROVER_SBUS_MAX_ALLOWED_FOOTERS = 4u,
};

enum rover_sbus_decode_status {
    ROVER_SBUS_DECODE_OK = 0,
    ROVER_SBUS_DECODE_BAD_ARGUMENT,
    ROVER_SBUS_DECODE_BAD_LENGTH,
    ROVER_SBUS_DECODE_BAD_HEADER,
    ROVER_SBUS_DECODE_BAD_FOOTER,
};

enum rover_sbus_parser_event {
    ROVER_SBUS_PARSER_NONE = 0,
    ROVER_SBUS_PARSER_FRAME,
    ROVER_SBUS_PARSER_REJECTED,
};

struct rover_sbus_config {
    uint8_t allowed_footers[ROVER_SBUS_MAX_ALLOWED_FOOTERS];
    size_t allowed_footer_count;
};

struct rover_sbus_frame {
    uint16_t channels[ROVER_G16_CHANNEL_COUNT];
    bool digital_channel_17;
    bool digital_channel_18;
    bool frame_lost;
    bool receiver_failsafe;
};

struct rover_sbus_parser {
    struct rover_sbus_config config;
    uint8_t bytes[ROVER_G16_SBUS_FRAME_SIZE];
    size_t count;
};

bool rover_sbus_config_is_valid(const struct rover_sbus_config *config);

enum rover_sbus_decode_status rover_sbus_decode(
    const uint8_t *bytes, size_t length, const struct rover_sbus_config *config,
    struct rover_sbus_frame *frame);

bool rover_sbus_parser_init(struct rover_sbus_parser *parser,
                            const struct rover_sbus_config *config);

enum rover_sbus_parser_event rover_sbus_parser_push(
    struct rover_sbus_parser *parser, uint8_t byte,
    struct rover_sbus_frame *frame);

bool rover_sbus_parser_on_gap(struct rover_sbus_parser *parser);

#endif
