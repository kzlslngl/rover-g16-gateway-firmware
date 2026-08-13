#include "register_snapshot.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
static portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t active_registers[ROVER_G16_REGISTER_COUNT];
void rover_register_snapshot_publish(const uint16_t registers[ROVER_G16_REGISTER_COUNT])
{
    taskENTER_CRITICAL(&snapshot_lock);
    memcpy(active_registers, registers, sizeof(active_registers));
    taskEXIT_CRITICAL(&snapshot_lock);
}
void rover_register_snapshot_copy(uint16_t registers[ROVER_G16_REGISTER_COUNT])
{
    taskENTER_CRITICAL(&snapshot_lock);
    memcpy(registers, active_registers, sizeof(active_registers));
    taskEXIT_CRITICAL(&snapshot_lock);
}
