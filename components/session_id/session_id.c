#include "session_id.h"

static const uint32_t FNV1A_OFFSET_BASIS = UINT32_C(2166136261);
static const uint32_t FNV1A_PRIME = UINT32_C(16777619);

static uint32_t fnv1a_byte(uint32_t hash, uint8_t byte)
{
    return (hash ^ byte) * FNV1A_PRIME;
}

static uint32_t fnv1a_u32_be(uint32_t hash, uint32_t value)
{
    hash = fnv1a_byte(hash, (uint8_t)(value >> 24));
    hash = fnv1a_byte(hash, (uint8_t)(value >> 16));
    hash = fnv1a_byte(hash, (uint8_t)(value >> 8));
    return fnv1a_byte(hash, (uint8_t)value);
}

uint32_t rover_session_id_derive(uint64_t device_id, uint32_t boot_counter,
                                 uint32_t random_value)
{
    uint32_t hash = FNV1A_OFFSET_BASIS;
    hash = fnv1a_u32_be(hash, UINT32_C(0x47313653)); /* "G16S" */
    hash = fnv1a_u32_be(hash, (uint32_t)(device_id >> 32));
    hash = fnv1a_u32_be(hash, (uint32_t)device_id);
    hash = fnv1a_u32_be(hash, boot_counter);
    hash = fnv1a_u32_be(hash, random_value);
    return hash == 0 ? UINT32_C(1) : hash;
}
