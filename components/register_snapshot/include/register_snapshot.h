#ifndef ROVER_REGISTER_SNAPSHOT_H
#define ROVER_REGISTER_SNAPSHOT_H
#include <stdint.h>
#include "protocol_version.h"
void rover_register_snapshot_publish(const uint16_t registers[ROVER_G16_REGISTER_COUNT]);
void rover_register_snapshot_copy(uint16_t registers[ROVER_G16_REGISTER_COUNT]);
#endif
