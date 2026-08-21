#include <inttypes.h>
#include <limits.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "ethernet_adapter.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "freshness.h"
#include "modbus_tcp_server.h"
#include "protocol_version.h"
#include "register_endian.h"
#include "register_image.h"
#include "register_snapshot.h"
#include "sbus_decoder.h"
#include "session_id.h"

static const char *TAG = "g16_gateway";

enum {
    SBUS_UART = UART_NUM_2,
    SBUS_RX_GPIO = GPIO_NUM_35,
    SBUS_BAUD_RATE = 100000,
    SBUS_RX_BUFFER_SIZE = 512,
    SBUS_READ_BUFFER_SIZE = 512,
    SBUS_STALE_TIMEOUT_MS = 100,
    SNAPSHOT_PUBLISH_PERIOD_MS = 20,
    SBUS_EVENT_QUEUE_SIZE = 20,
};

static QueueHandle_t sbus_event_queue;

static uint64_t monotonic_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

static void service_runtime_diagnostics(esp_task_wdt_user_handle_t watchdog,
                                        uint64_t now_us)
{
    ESP_ERROR_CHECK(esp_task_wdt_reset_user(watchdog));

    static uint64_t last_log_us;
    if (last_log_us != 0 && now_us - last_log_us < UINT64_C(10000000)) {
        return;
    }
    last_log_us = now_us;

    const struct rover_ethernet_status ethernet =
        rover_ethernet_get_status();
    const struct rover_modbus_tcp_status modbus =
        rover_modbus_tcp_server_get_status();
    ESP_LOGI(TAG,
             "health eth=%s ip=%s link_up=%" PRIu32
             " link_down=%" PRIu32 " ip_ready=%" PRIu32
             " client=%s connects=%" PRIu32 " requests=%" PRIu32
             " reads=%" PRIu32 " exceptions=%" PRIu32
             " timeouts=%" PRIu32 " transport_errors=%" PRIu32,
             ethernet.link_up ? "up" : "down",
             ethernet.has_ipv4 ? "ready" : "none",
             ethernet.link_up_count, ethernet.link_down_count,
             ethernet.ipv4_ready_count,
             modbus.client_connected ? "yes" : "no",
             modbus.client_connections, modbus.requests,
             modbus.successful_reads, modbus.exceptions,
             modbus.timeouts, modbus.transport_errors);
}

static uint32_t next_boot_counter(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open("g16_gateway", NVS_READWRITE, &handle));

    uint32_t boot_counter = 0;
    const esp_err_t read_status =
        nvs_get_u32(handle, "boot_count", &boot_counter);
    if (read_status != ESP_OK && read_status != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        ESP_ERROR_CHECK(read_status);
    }

    boot_counter = boot_counter == UINT32_MAX ? 1 : boot_counter + 1;
    ESP_ERROR_CHECK(nvs_set_u32(handle, "boot_count", boot_counter));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
    return boot_counter;
}

static uint32_t make_session_id(uint32_t boot_counter)
{
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY));
    const uint64_t device_id = ((uint64_t)mac[0] << 40) |
                               ((uint64_t)mac[1] << 32) |
                               ((uint64_t)mac[2] << 24) |
                               ((uint64_t)mac[3] << 16) |
                               ((uint64_t)mac[4] << 8) | mac[5];
    return rover_session_id_derive(device_id, boot_counter, esp_random());
}

static esp_err_t sbus_uart_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = SBUS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(SBUS_UART, SBUS_RX_BUFFER_SIZE, 0,
                                            SBUS_EVENT_QUEUE_SIZE,
                                            &sbus_event_queue, 0),
                        TAG, "UART driver install failed");
    ESP_RETURN_ON_ERROR(uart_param_config(SBUS_UART, &uart_config), TAG,
                        "UART configuration failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(SBUS_UART, UART_PIN_NO_CHANGE,
                                     SBUS_RX_GPIO, UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG, "UART pin configuration failed");
    ESP_RETURN_ON_ERROR(uart_set_line_inverse(SBUS_UART,
                                              UART_SIGNAL_RXD_INV),
                        TAG, "UART RX inversion failed");
    ESP_RETURN_ON_ERROR(uart_flush_input(SBUS_UART), TAG,
                        "UART input flush failed");
    return ESP_OK;
}

static void log_sbus_frame(const struct rover_sbus_frame *frame,
                           const struct rover_freshness_view *view,
                           const uint16_t *registers, uint32_t frame_count)
{
    const uint32_t sequence = rover_register_read_u32(
        registers, ROVER_G16_REG_BEGIN_SEQUENCE);
    const uint32_t crc = rover_register_read_u32(registers,
                                                  ROVER_G16_REG_CRC32);
    ESP_LOGI(TAG,
             "SBUS #%" PRIu32 " ch=[%u,%u,%u,%u,%u,%u,%u,%u,"
             "%u,%u,%u,%u,%u,%u,%u,%u] flags=0x%04x age=%ums "
             "period=%" PRIu32 "us seq=%" PRIu32 " crc=%08" PRIx32,
             frame_count, frame->channels[0], frame->channels[1],
             frame->channels[2], frame->channels[3], frame->channels[4],
             frame->channels[5], frame->channels[6], frame->channels[7],
             frame->channels[8], frame->channels[9], frame->channels[10],
             frame->channels[11], frame->channels[12], frame->channels[13],
             frame->channels[14], frame->channels[15], view->sbus_flags,
             view->frame_age_ms, view->frame_period_us, sequence, crc);
}

void app_main(void)
{
    ESP_LOGI(TAG, "protocol %u.%u, register base=%u, count=%u",
             ROVER_G16_PROTOCOL_VERSION_MAJOR,
             ROVER_G16_PROTOCOL_VERSION_MINOR,
             ROVER_G16_REGISTER_BASE,
             ROVER_G16_REGISTER_COUNT);

    ESP_ERROR_CHECK(sbus_uart_init());

    const struct rover_sbus_config sbus_config = {
        .allowed_footers = {0x00},
        .allowed_footer_count = 1,
    };
    struct rover_sbus_parser parser;
    ESP_ERROR_CHECK(rover_sbus_parser_init(&parser, &sbus_config)
                        ? ESP_OK
                        : ESP_ERR_INVALID_STATE);

    struct rover_freshness_state freshness_state;
    struct rover_freshness_view freshness_view;
    rover_freshness_init(&freshness_state);
    rover_freshness_set_alive(&freshness_state, true);

    struct rover_register_image_state image_state;
    const uint32_t boot_counter = next_boot_counter();
    const uint32_t session_id = make_session_id(boot_counter);
    ESP_ERROR_CHECK(rover_register_image_init(&image_state, session_id)
                        ? ESP_OK
                        : ESP_ERR_INVALID_STATE);
    uint16_t active_registers[ROVER_G16_REGISTER_COUNT];

    const uint64_t initial_now_us = monotonic_us();
    rover_freshness_make_view(&freshness_state, initial_now_us,
                              SBUS_STALE_TIMEOUT_MS, &freshness_view);
    ESP_ERROR_CHECK(rover_register_image_build(
                        &image_state, &freshness_view,
                        (uint32_t)(initial_now_us / 1000u), active_registers)
                        ? ESP_OK
                        : ESP_ERR_INVALID_STATE);
    rover_register_snapshot_publish(active_registers);
    ESP_ERROR_CHECK(rover_ethernet_start());
    ESP_ERROR_CHECK(rover_modbus_tcp_server_start());
    esp_task_wdt_user_handle_t watchdog;
    ESP_ERROR_CHECK(esp_task_wdt_add_user("sbus_pipeline", &watchdog));

    ESP_LOGI(TAG,
             "SBUS RX ready: UART%d GPIO%d 100000 8E2 inverted, footer=0x00, "
             "boot=%" PRIu32 " session=%08" PRIx32,
             SBUS_UART, SBUS_RX_GPIO, boot_counter, session_id);

    uint8_t bytes[SBUS_READ_BUFFER_SIZE];
    uint32_t frame_count = 0;
    uint32_t rejected_count = 0;
    uint64_t last_snapshot_us = initial_now_us;

    while (true) {
        const uint64_t loop_now_us = monotonic_us();
        service_runtime_diagnostics(watchdog, loop_now_us);
        uart_event_t uart_event;
        const bool has_event = xQueueReceive(sbus_event_queue, &uart_event,
                                              pdMS_TO_TICKS(20)) == pdTRUE;
        int length = 0;
        if (has_event && uart_event.type == UART_DATA) {
            const size_t requested = uart_event.size < sizeof(bytes)
                                         ? uart_event.size
                                         : sizeof(bytes);
            length = uart_read_bytes(SBUS_UART, bytes, requested, 0);
        } else if (has_event &&
                   (uart_event.type == UART_PARITY_ERR ||
                    uart_event.type == UART_FRAME_ERR)) {
            rover_freshness_record_invalid(&freshness_state);
            rover_sbus_parser_init(&parser, &sbus_config);
            ESP_LOGW(TAG, "UART line error type=%d", uart_event.type);
        } else if (has_event &&
                   (uart_event.type == UART_FIFO_OVF ||
                    uart_event.type == UART_BUFFER_FULL)) {
            rover_freshness_record_invalid(&freshness_state);
            rover_freshness_set_fault(&freshness_state, true);
            uart_flush_input(SBUS_UART);
            xQueueReset(sbus_event_queue);
            rover_sbus_parser_init(&parser, &sbus_config);
            ESP_LOGE(TAG, "UART overflow type=%d; parser reset",
                     uart_event.type);
        }

        if (!has_event) {
            if (rover_sbus_parser_on_gap(&parser)) {
                rover_freshness_record_invalid(&freshness_state);
            }
        } else if (length > 0) {
            for (int index = 0; index < length; ++index) {
                struct rover_sbus_frame frame;
                const enum rover_sbus_parser_event event =
                    rover_sbus_parser_push(&parser, bytes[index], &frame);
                if (event == ROVER_SBUS_PARSER_FRAME) {
                    ++frame_count;
                    const uint64_t frame_now_us = monotonic_us();
                    rover_freshness_record_frame(&freshness_state, &frame,
                                                  frame_now_us);
                    rover_freshness_set_fault(&freshness_state, false);
                    if (frame_count <= 10 || frame_count % 25 == 0) {
                        rover_freshness_make_view(
                            &freshness_state, frame_now_us,
                            SBUS_STALE_TIMEOUT_MS, &freshness_view);
                        log_sbus_frame(&frame, &freshness_view,
                                       active_registers, frame_count);
                    }
                } else if (event == ROVER_SBUS_PARSER_REJECTED) {
                    ++rejected_count;
                    rover_freshness_record_invalid(&freshness_state);
                    if (rejected_count <= 5 || rejected_count % 100 == 0) {
                        ESP_LOGW(TAG, "rejected SBUS frames: %" PRIu32,
                                 rejected_count);
                    }
                }
            }
        }

        const uint64_t publish_now_us = monotonic_us();
        if (publish_now_us - last_snapshot_us >=
            (uint64_t)SNAPSHOT_PUBLISH_PERIOD_MS * 1000u) {
            rover_freshness_make_view(&freshness_state, publish_now_us,
                                       SBUS_STALE_TIMEOUT_MS,
                                       &freshness_view);
            ESP_ERROR_CHECK(rover_register_image_build(
                                &image_state, &freshness_view,
                                (uint32_t)(publish_now_us / 1000u),
                                active_registers)
                                ? ESP_OK
                                : ESP_ERR_INVALID_STATE);
            rover_register_snapshot_publish(active_registers);
            last_snapshot_us = publish_now_us;
        }
    }
}
