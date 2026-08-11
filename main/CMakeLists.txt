idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "../protocol"
                       REQUIRES crc32 driver register_image sbus_decoder session_id)
