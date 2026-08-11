#ifndef ROVER_G16_SESSION_ID_H
#define ROVER_G16_SESSION_ID_H

#include <stdint.h>

uint32_t rover_session_id_derive(uint64_t device_id, uint32_t boot_counter,
                                 uint32_t random_value);

#endif
