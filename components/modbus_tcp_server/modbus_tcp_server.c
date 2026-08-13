#include "modbus_tcp_server.h"
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "protocol_version.h"
#include "register_snapshot.h"

static const char *TAG = "g16_modbus";
enum {
    MODBUS_TCP_PORT = 502, MODBUS_UNIT_ID = 1, MODBUS_FC_READ_HOLDING = 3,
    MODBUS_EXCEPTION_ILLEGAL_FUNCTION = 1, MODBUS_EXCEPTION_ILLEGAL_ADDRESS = 2,
    MODBUS_EXCEPTION_ILLEGAL_VALUE = 3, MODBUS_MBAP_SIZE = 7,
    MODBUS_REQUEST_PDU_SIZE = 5, MODBUS_MAX_ADU_SIZE = 260,
};

static bool recv_all(int fd, uint8_t *buffer, size_t length)
{
    size_t done = 0;
    while (done < length) {
        const int count = recv(fd, buffer + done, length - done, 0);
        if (count <= 0) return false;
        done += (size_t)count;
    }
    return true;
}

static bool send_all(int fd, const uint8_t *buffer, size_t length)
{
    size_t done = 0;
    while (done < length) {
        const int count = send(fd, buffer + done, length - done, 0);
        if (count <= 0) return false;
        done += (size_t)count;
    }
    return true;
}

static size_t make_exception(uint8_t *response, const uint8_t *request,
                             uint8_t function, uint8_t exception)
{
    memcpy(response, request, 4);
    response[4] = 0; response[5] = 3; response[6] = request[6];
    response[7] = function | UINT8_C(0x80); response[8] = exception;
    return 9;
}

static size_t handle_request(const uint8_t *request, size_t request_length,
                             uint8_t response[MODBUS_MAX_ADU_SIZE])
{
    const uint8_t function = request_length > MODBUS_MBAP_SIZE ? request[7] : 0;
    if (request_length != MODBUS_MBAP_SIZE + MODBUS_REQUEST_PDU_SIZE ||
        request[2] != 0 || request[3] != 0) {
        return make_exception(response, request, function, MODBUS_EXCEPTION_ILLEGAL_VALUE);
    }
    if (request[6] != MODBUS_UNIT_ID) {
        return make_exception(response, request, function, MODBUS_EXCEPTION_ILLEGAL_ADDRESS);
    }
    if (function != MODBUS_FC_READ_HOLDING) {
        return make_exception(response, request, function, MODBUS_EXCEPTION_ILLEGAL_FUNCTION);
    }
    const uint16_t start = ((uint16_t)request[8] << 8) | request[9];
    const uint16_t quantity = ((uint16_t)request[10] << 8) | request[11];
    if (start != ROVER_G16_REGISTER_BASE || quantity != ROVER_G16_REGISTER_COUNT) {
        return make_exception(response, request, function, MODBUS_EXCEPTION_ILLEGAL_ADDRESS);
    }
    uint16_t registers[ROVER_G16_REGISTER_COUNT];
    rover_register_snapshot_copy(registers);
    memcpy(response, request, 4);
    const uint16_t mbap_length = 3 + (2 * ROVER_G16_REGISTER_COUNT);
    response[4] = (uint8_t)(mbap_length >> 8); response[5] = (uint8_t)mbap_length;
    response[6] = MODBUS_UNIT_ID; response[7] = MODBUS_FC_READ_HOLDING;
    response[8] = 2 * ROVER_G16_REGISTER_COUNT;
    for (size_t index = 0; index < ROVER_G16_REGISTER_COUNT; ++index) {
        response[9 + 2 * index] = (uint8_t)(registers[index] >> 8);
        response[10 + 2 * index] = (uint8_t)registers[index];
    }
    return 9 + (2 * ROVER_G16_REGISTER_COUNT);
}

static void serve_client(int fd)
{
    uint8_t request[MODBUS_MAX_ADU_SIZE], response[MODBUS_MAX_ADU_SIZE];
    while (recv_all(fd, request, MODBUS_MBAP_SIZE)) {
        const uint16_t length = ((uint16_t)request[4] << 8) | request[5];
        if (length < 2 || length > MODBUS_MAX_ADU_SIZE - 6) break;
        const size_t remaining = (size_t)length - 1;
        if (!recv_all(fd, request + MODBUS_MBAP_SIZE, remaining)) break;
        const size_t response_length = handle_request(
            request, MODBUS_MBAP_SIZE + remaining, response);
        if (!send_all(fd, response, response_length)) break;
    }
}

static void server_task(void *argument)
{
    (void)argument;
    while (true) {
        const int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (listen_fd < 0) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        const int enable = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
        const struct sockaddr_in address = {
            .sin_family = AF_INET, .sin_port = htons(MODBUS_TCP_PORT),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };
        if (bind(listen_fd, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
            listen(listen_fd, 1) != 0) {
            ESP_LOGE(TAG, "bind/listen failed: errno=%d", errno);
            close(listen_fd); vTaskDelay(pdMS_TO_TICKS(1000)); continue;
        }
        ESP_LOGI(TAG, "read-only server listening on TCP/%d", MODBUS_TCP_PORT);
        while (true) {
            struct sockaddr_storage source; socklen_t source_length = sizeof(source);
            const int client_fd = accept(listen_fd, (struct sockaddr *)&source, &source_length);
            if (client_fd < 0) break;
            ESP_LOGI(TAG, "client connected");
            serve_client(client_fd);
            shutdown(client_fd, SHUT_RDWR); close(client_fd);
            ESP_LOGI(TAG, "client disconnected");
        }
        close(listen_fd);
    }
}

esp_err_t rover_modbus_tcp_server_start(void)
{
    return xTaskCreate(server_task, "g16_modbus", 4096, NULL, 5, NULL) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}
