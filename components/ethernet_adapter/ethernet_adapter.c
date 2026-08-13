#include "ethernet_adapter.h"

#include <string.h>

#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "g16_ethernet";

enum {
    ROVER_ETH_PHY_ADDRESS = 1,
    ROVER_ETH_PHY_RESET_GPIO = 16,
    ROVER_ETH_MDC_GPIO = 23,
    ROVER_ETH_MDIO_GPIO = 18,
};

static SemaphoreHandle_t status_mutex;
static struct rover_ethernet_status status;

static void record_link_status(bool link_up)
{
    if (xSemaphoreTake(status_mutex, portMAX_DELAY) == pdTRUE) {
        status.link_up = link_up;
        if (link_up) {
            ++status.link_up_count;
        } else {
            ++status.link_down_count;
        }
        if (!link_up) {
            status.has_ipv4 = false;
            status.ipv4.addr = 0;
        }
        xSemaphoreGive(status_mutex);
    }
}

static void on_ethernet_event(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        record_link_status(true);
        ESP_LOGI(TAG, "link up; applying static IPv4");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        record_link_status(false);
        ESP_LOGW(TAG, "link down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "interface started");
        break;
    case ETHERNET_EVENT_STOP:
        record_link_status(false);
        ESP_LOGI(TAG, "interface stopped");
        break;
    default:
        break;
    }
}

static void on_got_ipv4(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_id;
    const ip_event_got_ip_t *event = event_data;

    if (xSemaphoreTake(status_mutex, portMAX_DELAY) == pdTRUE) {
        status.link_up = true;
        status.has_ipv4 = true;
        status.ipv4 = event->ip_info.ip;
        ++status.ipv4_ready_count;
        xSemaphoreGive(status_mutex);
    }

    ESP_LOGI(TAG, "IPv4 ready: address=" IPSTR " gateway=" IPSTR " netmask=" IPSTR,
             IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.gw),
             IP2STR(&event->ip_info.netmask));
}

struct rover_ethernet_status rover_ethernet_get_status(void)
{
    struct rover_ethernet_status copy = {0};
    if (status_mutex != NULL &&
        xSemaphoreTake(status_mutex, portMAX_DELAY) == pdTRUE) {
        copy = status;
        xSemaphoreGive(status_mutex);
    }
    return copy;
}

esp_err_t rover_ethernet_start(void)
{
    if (status_mutex != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    status_mutex = xSemaphoreCreateMutex();
    if (status_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(&status, 0, sizeof(status));

    esp_err_t result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }

    const esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&netif_config);
    if (netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    result = esp_netif_dhcpc_stop(netif);
    if (result != ESP_OK && result != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        return result;
    }
    esp_netif_ip_info_t ip_info = {0};
    if (esp_netif_str_to_ip4(CONFIG_ROVER_ETH_IPV4_ADDR, &ip_info.ip) != ESP_OK ||
        esp_netif_str_to_ip4(CONFIG_ROVER_ETH_IPV4_GATEWAY, &ip_info.gw) != ESP_OK ||
        esp_netif_str_to_ip4(CONFIG_ROVER_ETH_IPV4_NETMASK,
                            &ip_info.netmask) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    result = esp_netif_set_ip_info(netif, &ip_info);
    if (result != ESP_OK) {
        return result;
    }

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_config.smi_gpio.mdc_num = ROVER_ETH_MDC_GPIO;
    emac_config.smi_gpio.mdio_num = ROVER_ETH_MDIO_GPIO;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    if (mac == NULL) {
        return ESP_ERR_NO_MEM;
    }

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = ROVER_ETH_PHY_ADDRESS;
    phy_config.reset_gpio_num = ROVER_ETH_PHY_RESET_GPIO;
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);
    if (phy == NULL) {
        mac->del(mac);
        return ESP_ERR_NO_MEM;
    }

    esp_eth_handle_t eth_handle = NULL;
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    result = esp_eth_driver_install(&eth_config, &eth_handle);
    if (result != ESP_OK) {
        phy->del(phy);
        mac->del(mac);
        return result;
    }

    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(eth_handle);
    if (glue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    result = esp_netif_attach(netif, glue);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                        on_ethernet_event, NULL);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                        on_got_ipv4, NULL);
    if (result != ESP_OK) {
        return result;
    }

    ESP_LOGI(TAG,
             "LAN8720 RMII: addr=%d reset=%d MDC=%d MDIO=%d clock=GPIO0 input",
             ROVER_ETH_PHY_ADDRESS, ROVER_ETH_PHY_RESET_GPIO,
             ROVER_ETH_MDC_GPIO, ROVER_ETH_MDIO_GPIO);
    ESP_LOGI(TAG, "static IPv4=%s gateway=%s netmask=%s",
             CONFIG_ROVER_ETH_IPV4_ADDR, CONFIG_ROVER_ETH_IPV4_GATEWAY,
             CONFIG_ROVER_ETH_IPV4_NETMASK);
    return esp_eth_start(eth_handle);
}
