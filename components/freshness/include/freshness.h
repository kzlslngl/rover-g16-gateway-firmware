#ifndef ROVER_G16_FRESHNESS_H
#define ROVER_G16_FRESHNESS_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol_version.h"
#include "sbus_decoder.h"

struct rover_freshness_state {
    uint16_t channels[ROVER_G16_CHANNEL_COUNT];
    uint64_t last_usable_frame_us;
    uint64_t period_sample_us;
    uint32_t frame_period_us;
    uint32_t frames_since_period_sample;
    uint32_t sbus_frame_counter;
    uint32_t invalid_frame_count;
    uint32_t frame_lost_count;
    uint32_t failsafe_count;
    bool has_usable_frame;
    bool decoder_alive;
    bool decoder_fault;
    bool last_structural_frame_lost;
    bool last_structural_failsafe;
};

struct rover_freshness_view {
    uint16_t channels[ROVER_G16_CHANNEL_COUNT];
    uint32_t frame_period_us;
    uint32_t sbus_frame_counter;
    uint32_t invalid_frame_count;
    uint32_t frame_lost_count;
    uint32_t failsafe_count;
    uint16_t frame_age_ms;
    uint16_t sbus_flags;
    uint16_t channel_valid_mask;
};

void rover_freshness_init(struct rover_freshness_state *state);
void rover_freshness_set_alive(struct rover_freshness_state *state,
                               bool alive);
void rover_freshness_set_fault(struct rover_freshness_state *state,
                               bool fault);
void rover_freshness_record_invalid(struct rover_freshness_state *state);
bool rover_freshness_record_frame(struct rover_freshness_state *state,
                                  const struct rover_sbus_frame *frame,
                                  uint64_t now_us);
void rover_freshness_make_view(const struct rover_freshness_state *state,
                               uint64_t now_us, uint32_t stale_timeout_ms,
                               struct rover_freshness_view *view);

#endif
