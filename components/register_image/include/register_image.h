#ifndef ROVER_G16_REGISTER_IMAGE_H
#define ROVER_G16_REGISTER_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "freshness.h"
#include "protocol_version.h"

struct rover_register_image_state {
    uint32_t gateway_session_id;
    uint32_t gateway_heartbeat;
    uint32_t sequence;
};

bool rover_register_image_init(struct rover_register_image_state *state,
                               uint32_t gateway_session_id);

bool rover_register_image_build(
    struct rover_register_image_state *state,
    const struct rover_freshness_view *freshness, uint32_t monotonic_ms,
    uint16_t registers[ROVER_G16_REGISTER_COUNT]);

#endif
