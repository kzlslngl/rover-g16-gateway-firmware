idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "../protocol"
                       REQUIRES crc32 driver esp_hw_support esp_timer freshness
                                nvs_flash register_image sbus_decoder session_id)
