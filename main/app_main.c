#include <inttypes.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"

#include "protocol_version.h"
#include "sbus_decoder.h"

static const char *TAG = "g16_gateway";

enum {
    SBUS_UART = UART_NUM_2,
    SBUS_RX_GPIO = GPIO_NUM_35,
    SBUS_BAUD_RATE = 100000,
    SBUS_RX_BUFFER_SIZE = 512,
    SBUS_READ_BUFFER_SIZE = 128,
};

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
                                            0, NULL, 0),
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
                           uint32_t frame_count)
{
    ESP_LOGI(TAG,
             "SBUS #%" PRIu32 " ch=[%u,%u,%u,%u,%u,%u,%u,%u,"
             "%u,%u,%u,%u,%u,%u,%u,%u] lost=%u failsafe=%u",
             frame_count, frame->channels[0], frame->channels[1],
             frame->channels[2], frame->channels[3], frame->channels[4],
             frame->channels[5], frame->channels[6], frame->channels[7],
             frame->channels[8], frame->channels[9], frame->channels[10],
             frame->channels[11], frame->channels[12], frame->channels[13],
             frame->channels[14], frame->channels[15], frame->frame_lost,
             frame->receiver_failsafe);
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

    ESP_LOGI(TAG,
             "SBUS RX ready: UART%d GPIO%d 100000 8E2 inverted, footer=0x00",
             SBUS_UART, SBUS_RX_GPIO);

    uint8_t bytes[SBUS_READ_BUFFER_SIZE];
    uint32_t frame_count = 0;
    uint32_t rejected_count = 0;

    while (true) {
        const int length = uart_read_bytes(SBUS_UART, bytes, sizeof(bytes),
                                           pdMS_TO_TICKS(20));
        if (length <= 0) {
            rover_sbus_parser_on_gap(&parser);
            continue;
        }

        for (int index = 0; index < length; ++index) {
            struct rover_sbus_frame frame;
            const enum rover_sbus_parser_event event =
                rover_sbus_parser_push(&parser, bytes[index], &frame);
            if (event == ROVER_SBUS_PARSER_FRAME) {
                ++frame_count;
                if (frame_count <= 10 || frame_count % 25 == 0) {
                    log_sbus_frame(&frame, frame_count);
                }
            } else if (event == ROVER_SBUS_PARSER_REJECTED) {
                ++rejected_count;
                if (rejected_count <= 5 || rejected_count % 100 == 0) {
                    ESP_LOGW(TAG, "rejected SBUS frames: %" PRIu32,
                             rejected_count);
                }
            }
        }
    }
}
