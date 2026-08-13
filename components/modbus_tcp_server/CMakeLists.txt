idf_component_register(SRCS "modbus_tcp_server.c"
                       INCLUDE_DIRS "include" "../../protocol"
                       REQUIRES esp_common freertos lwip register_snapshot)
