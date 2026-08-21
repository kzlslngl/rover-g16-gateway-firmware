#include "sbus_decoder.h"

#include <string.h>

enum {
    SBUS_PAYLOAD_FIRST_BYTE = 1u,
    SBUS_FLAGS_BYTE = 23u,
    SBUS_FOOTER_BYTE = 24u,
    SBUS_DIGITAL_CHANNEL_17_MASK = 1u << 0,
    SBUS_DIGITAL_CHANNEL_18_MASK = 1u << 1,
    SBUS_FRAME_LOST_MASK = 1u << 2,
    SBUS_FAILSAFE_MASK = 1u << 3,
    SBUS_CHANNEL_MASK = 0x07FFu,
};

static bool footer_is_allowed(const struct rover_sbus_config *config,
                              uint8_t footer)
{
    for (size_t index = 0; index < config->allowed_footer_count; ++index) {
        if (config->allowed_footers[index] == footer) {
            return true;
        }
    }
    return false;
}

bool rover_sbus_config_is_valid(const struct rover_sbus_config *config)
{
    return config != NULL && config->allowed_footer_count > 0 &&
           config->allowed_footer_count <= ROVER_SBUS_MAX_ALLOWED_FOOTERS;
}

static uint16_t decode_channel(const uint8_t *payload, size_t channel)
{
    const size_t bit_offset = channel * 11u;
    const size_t byte_offset = bit_offset / 8u;
    const unsigned shift = (unsigned)(bit_offset % 8u);
    uint32_t window = payload[byte_offset];

    window |= (uint32_t)payload[byte_offset + 1u] << 8;
    if (byte_offset + 2u < 22u) {
        window |= (uint32_t)payload[byte_offset + 2u] << 16;
    }

    return (uint16_t)((window >> shift) & SBUS_CHANNEL_MASK);
}

enum rover_sbus_decode_status rover_sbus_decode(
    const uint8_t *bytes, size_t length, const struct rover_sbus_config *config,
    struct rover_sbus_frame *frame)
{
    if (bytes == NULL || frame == NULL || !rover_sbus_config_is_valid(config)) {
        return ROVER_SBUS_DECODE_BAD_ARGUMENT;
    }
    if (length != ROVER_G16_SBUS_FRAME_SIZE) {
        return ROVER_SBUS_DECODE_BAD_LENGTH;
    }
    if (bytes[0] != ROVER_SBUS_HEADER) {
        return ROVER_SBUS_DECODE_BAD_HEADER;
    }
    if (!footer_is_allowed(config, bytes[SBUS_FOOTER_BYTE])) {
        return ROVER_SBUS_DECODE_BAD_FOOTER;
    }

    for (size_t channel = 0; channel < ROVER_G16_CHANNEL_COUNT; ++channel) {
        frame->channels[channel] =
            decode_channel(&bytes[SBUS_PAYLOAD_FIRST_BYTE], channel);
    }

    const uint8_t flags = bytes[SBUS_FLAGS_BYTE];
    frame->digital_channel_17 = (flags & SBUS_DIGITAL_CHANNEL_17_MASK) != 0;
    frame->digital_channel_18 = (flags & SBUS_DIGITAL_CHANNEL_18_MASK) != 0;
    frame->frame_lost = (flags & SBUS_FRAME_LOST_MASK) != 0;
    frame->receiver_failsafe = (flags & SBUS_FAILSAFE_MASK) != 0;
    return ROVER_SBUS_DECODE_OK;
}

bool rover_sbus_parser_init(struct rover_sbus_parser *parser,
                            const struct rover_sbus_config *config)
{
    if (parser == NULL || !rover_sbus_config_is_valid(config)) {
        return false;
    }

    memset(parser, 0, sizeof(*parser));
    parser->config = *config;
    return true;
}

static void parser_resync(struct rover_sbus_parser *parser)
{
    size_t header = 1u;
    while (header < parser->count &&
           parser->bytes[header] != ROVER_SBUS_HEADER) {
        ++header;
    }

    if (header == parser->count) {
        parser->count = 0;
        return;
    }

    const size_t remaining = parser->count - header;
    memmove(parser->bytes, &parser->bytes[header], remaining);
    parser->count = remaining;
}

enum rover_sbus_parser_event rover_sbus_parser_push(
    struct rover_sbus_parser *parser, uint8_t byte,
    struct rover_sbus_frame *frame)
{
    if (parser == NULL || frame == NULL) {
        return ROVER_SBUS_PARSER_REJECTED;
    }

    if (parser->count == 0 && byte != ROVER_SBUS_HEADER) {
        return ROVER_SBUS_PARSER_NONE;
    }

    parser->bytes[parser->count++] = byte;
    if (parser->count < ROVER_G16_SBUS_FRAME_SIZE) {
        return ROVER_SBUS_PARSER_NONE;
    }

    const enum rover_sbus_decode_status status =
        rover_sbus_decode(parser->bytes, parser->count, &parser->config, frame);
    if (status == ROVER_SBUS_DECODE_OK) {
        parser->count = 0;
        return ROVER_SBUS_PARSER_FRAME;
    }

    parser_resync(parser);
    return ROVER_SBUS_PARSER_REJECTED;
}

bool rover_sbus_parser_on_gap(struct rover_sbus_parser *parser)
{
    if (parser == NULL) {
        return false;
    }

    const bool discarded_partial_frame = parser->count != 0;
    parser->count = 0;
    return discarded_partial_frame;
}
