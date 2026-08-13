#ifndef ROVER_MODBUS_TCP_SERVER_H
#define ROVER_MODBUS_TCP_SERVER_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

struct rover_modbus_tcp_status {
    bool client_connected;
    uint32_t client_connections;
    uint32_t requests;
    uint32_t successful_reads;
    uint32_t exceptions;
    uint32_t timeouts;
    uint32_t transport_errors;
};

esp_err_t rover_modbus_tcp_server_start(void);
struct rover_modbus_tcp_status rover_modbus_tcp_server_get_status(void);
#endif
