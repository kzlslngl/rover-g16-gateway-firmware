#ifndef ROVER_ETHERNET_ADAPTER_H
#define ROVER_ETHERNET_ADAPTER_H

#include <stdbool.h>

#include "esp_err.h"
#include "esp_netif_ip_addr.h"

struct rover_ethernet_status {
    bool link_up;
    bool has_ipv4;
    esp_ip4_addr_t ipv4;
};

/* Starts ESP32 EMAC/LAN8720 asynchronously with the project static IPv4. */
esp_err_t rover_ethernet_start(void);

/* Returns a point-in-time copy suitable for diagnostics and later protocol use. */
struct rover_ethernet_status rover_ethernet_get_status(void);

#endif
