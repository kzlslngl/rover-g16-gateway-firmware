idf_component_register(SRCS "ethernet_adapter.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_eth esp_event esp_netif)
