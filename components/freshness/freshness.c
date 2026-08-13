#include "freshness.h"

#include <limits.h>
#include <string.h>

static uint32_t saturate_u64_to_u32(uint64_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

enum {
    PERIOD_SAMPLE_MIN_ELAPSED_US = 1000,
    SBUS_PERIOD_MIN_US = 5000,
    SBUS_PERIOD_MAX_US = 20000,
};

static bool period_sample_is_plausible(uint64_t sample)
{
    return sample >= SBUS_PERIOD_MIN_US && sample <= SBUS_PERIOD_MAX_US;
}

void rover_freshness_init(struct rover_freshness_state *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

void rover_freshness_set_alive(struct rover_freshness_state *state,
                               bool alive)
{
    if (state != NULL) {
        state->decoder_alive = alive;
    }
}

void rover_freshness_set_fault(struct rover_freshness_state *state,
                               bool fault)
{
    if (state != NULL) {
        state->decoder_fault = fault;
    }
}

void rover_freshness_record_invalid(struct rover_freshness_state *state)
{
    if (state != NULL) {
        ++state->invalid_frame_count;
    }
}

bool rover_freshness_record_frame(struct rover_freshness_state *state,
                                  const struct rover_sbus_frame *frame,
                                  uint64_t now_us)
{
    if (state == NULL || frame == NULL) {
        return false;
    }

    state->last_structural_frame_lost = frame->frame_lost;
    state->last_structural_failsafe = frame->receiver_failsafe;
    if (frame->frame_lost) {
        ++state->frame_lost_count;
    }
    if (frame->receiver_failsafe) {
        ++state->failsafe_count;
    }
    if (frame->frame_lost || frame->receiver_failsafe) {
        return false;
    }

    if (state->has_usable_frame) {
        ++state->frames_since_period_sample;
        const uint64_t elapsed = now_us >= state->period_sample_us
                                     ? now_us - state->period_sample_us
                                     : UINT64_MAX;
        if (elapsed >= PERIOD_SAMPLE_MIN_ELAPSED_US) {
            const uint64_t average =
                elapsed / state->frames_since_period_sample;
            if (period_sample_is_plausible(average)) {
                state->frame_period_us = saturate_u64_to_u32(average);
            }
            state->period_sample_us = now_us;
            state->frames_since_period_sample = 0;
        }
    } else {
        state->period_sample_us = now_us;
    }
    memcpy(state->channels, frame->channels, sizeof(state->channels));
    state->last_usable_frame_us = now_us;
    state->has_usable_frame = true;
    ++state->sbus_frame_counter;
    return true;
}

void rover_freshness_make_view(const struct rover_freshness_state *state,
                               uint64_t now_us, uint32_t stale_timeout_ms,
                               struct rover_freshness_view *view)
{
    if (state == NULL || view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    memcpy(view->channels, state->channels, sizeof(view->channels));
    view->frame_period_us = state->frame_period_us;
    view->sbus_frame_counter = state->sbus_frame_counter;
    view->invalid_frame_count = state->invalid_frame_count;
    view->frame_lost_count = state->frame_lost_count;
    view->failsafe_count = state->failsafe_count;

    uint64_t age_ms = UINT64_MAX;
    if (state->has_usable_frame && now_us >= state->last_usable_frame_us) {
        age_ms = (now_us - state->last_usable_frame_us) / 1000u;
    }
    view->frame_age_ms =
        age_ms > UINT16_MAX ? UINT16_MAX : (uint16_t)age_ms;

    if (state->last_structural_frame_lost) {
        view->sbus_flags |= ROVER_G16_SBUS_FLAG_FRAME_LOST;
    }
    if (state->last_structural_failsafe) {
        view->sbus_flags |= ROVER_G16_SBUS_FLAG_FAILSAFE;
    }
    if (state->decoder_alive) {
        view->sbus_flags |= ROVER_G16_SBUS_FLAG_ALIVE;
    }
    if (state->decoder_fault) {
        view->sbus_flags |= ROVER_G16_SBUS_FLAG_FAULT;
    }

    const bool valid = state->has_usable_frame &&
                       age_ms <= stale_timeout_ms &&
                       !state->last_structural_frame_lost &&
                       !state->last_structural_failsafe &&
                       !state->decoder_fault;
    if (valid) {
        view->sbus_flags |= ROVER_G16_SBUS_FLAG_VALID;
        view->channel_valid_mask = UINT16_MAX;
    }
}
