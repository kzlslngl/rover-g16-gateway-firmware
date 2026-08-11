#include "esp_log.h"

#include "protocol_version.h"

static const char *TAG = "g16_gateway";

void app_main(void)
{
    ESP_LOGI(TAG, "protocol %u.%u, register base=%u, count=%u",
             ROVER_G16_PROTOCOL_VERSION_MAJOR,
             ROVER_G16_PROTOCOL_VERSION_MINOR,
             ROVER_G16_REGISTER_BASE,
             ROVER_G16_REGISTER_COUNT);
}
