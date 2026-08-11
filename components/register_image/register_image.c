#include "register_image.h"

#include <string.h>

#include "crc32_iso_hdlc.h"
#include "register_endian.h"

bool rover_register_image_init(struct rover_register_image_state *state,
                               uint32_t gateway_session_id)
{
    if (state == NULL || gateway_session_id == 0) {
        return false;
    }

    memset(state, 0, sizeof(*state));
    state->gateway_session_id = gateway_session_id;
    return true;
}

bool rover_register_image_build(
    struct rover_register_image_state *state,
    const struct rover_freshness_view *freshness, uint32_t monotonic_ms,
    uint16_t registers[ROVER_G16_REGISTER_COUNT])
{
    if (state == NULL || freshness == NULL || registers == NULL ||
        state->gateway_session_id == 0) {
        return false;
    }

    ++state->sequence;
    ++state->gateway_heartbeat;
    memset(registers, 0,
           sizeof(uint16_t) * (size_t)ROVER_G16_REGISTER_COUNT);

    registers[ROVER_G16_REG_MAGIC] = ROVER_G16_PROTOCOL_MAGIC;
    registers[ROVER_G16_REG_VERSION_MAJOR] =
        ROVER_G16_PROTOCOL_VERSION_MAJOR;
    registers[ROVER_G16_REG_VERSION_MINOR] =
        ROVER_G16_PROTOCOL_VERSION_MINOR;
    registers[ROVER_G16_REG_LENGTH] = ROVER_G16_REGISTER_COUNT;
    rover_register_write_u32(registers, ROVER_G16_REG_BEGIN_SEQUENCE,
                             state->sequence);
    rover_register_write_u32(registers, ROVER_G16_REG_SESSION_ID,
                             state->gateway_session_id);
    rover_register_write_u32(registers, ROVER_G16_REG_HEARTBEAT,
                             state->gateway_heartbeat);
    rover_register_write_u32(registers, ROVER_G16_REG_SBUS_FRAME_COUNTER,
                             freshness->sbus_frame_counter);
    rover_register_write_u32(registers, ROVER_G16_REG_MONOTONIC_MS,
                             monotonic_ms);
    registers[ROVER_G16_REG_FRAME_AGE_MS] = freshness->frame_age_ms;
    registers[ROVER_G16_REG_SBUS_FLAGS] = freshness->sbus_flags;
    registers[ROVER_G16_REG_CHANNEL_COUNT] = ROVER_G16_CHANNEL_COUNT;
    registers[ROVER_G16_REG_CHANNEL_VALID_MASK] =
        freshness->channel_valid_mask;
    memcpy(&registers[ROVER_G16_REG_CHANNEL_RAW], freshness->channels,
           sizeof(freshness->channels));
    rover_register_write_u32(registers, ROVER_G16_REG_FRAME_PERIOD_US,
                             freshness->frame_period_us);
    rover_register_write_u32(registers, ROVER_G16_REG_INVALID_FRAME_COUNT,
                             freshness->invalid_frame_count);
    rover_register_write_u32(registers, ROVER_G16_REG_FRAME_LOST_COUNT,
                             freshness->frame_lost_count);
    rover_register_write_u32(registers, ROVER_G16_REG_FAILSAFE_COUNT,
                             freshness->failsafe_count);

    const uint32_t crc = rover_crc32_registers_be(
        registers, ROVER_G16_CRC_INPUT_REGISTER_COUNT);
    rover_register_write_u32(registers, ROVER_G16_REG_CRC32, crc);
    rover_register_write_u32(registers, ROVER_G16_REG_END_SEQUENCE,
                             state->sequence);
    return true;
}
