idf_component_register(SRCS "register_endian.c" "register_image.c"
                       INCLUDE_DIRS "include" "../../protocol"
                       REQUIRES crc32 freshness)
