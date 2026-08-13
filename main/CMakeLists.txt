idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "../protocol"
                       REQUIRES crc32 driver esp_hw_support esp_timer ethernet_adapter freshness
                                modbus_tcp_server nvs_flash register_image register_snapshot
                                sbus_decoder session_id)
