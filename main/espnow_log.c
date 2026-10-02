/* SPDX-License-Identifier: Apache-2.0 */

#include "espnow_log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG "vocat_espnow_log"
#define LOG_QUEUE_LENGTH 32
#define LOG_TEXT_CAPACITY 768
#define SEND_RETRIES 3
#define TX_WAIT_MS 500
#define ACK_WAIT_MS 700
#define ESPNOW_LOG_MAGIC 0x56434C47u /* "VCLG" */
#define ESPNOW_LOG_VERSION 1
#define ESPNOW_LOG_PAYLOAD_MAX 234

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint32_t sequence;
    uint8_t fragment_index;
    uint8_t fragment_count;
    uint16_t payload_length;
    uint16_t total_length;
    uint8_t payload[ESP_NOW_MAX_DATA_LEN - 16];
} __attribute__((packed)) log_packet_t;

enum {
    PACKET_HELLO = 1,
    PACKET_LOG = 2,
    PACKET_ACK = 3,
};

typedef struct {
    uint16_t length;
    char text[LOG_TEXT_CAPACITY];
} log_record_t;

static QueueHandle_t s_log_queue;
static SemaphoreHandle_t s_tx_result_semaphore;
static SemaphoreHandle_t s_ack_semaphore;
static TaskHandle_t s_tx_task;
static portMUX_TYPE s_peer_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_peer_mac[ESP_NOW_ETH_ALEN];
static bool s_peer_known;
static volatile bool s_last_tx_succeeded;
static volatile uint32_t s_waiting_ack_sequence;
static volatile uint32_t s_dropped_records;
static uint32_t s_next_sequence = 1;
static vprintf_like_t s_console_vprintf;

#if CONFIG_VOCAT_DEBUG_UART_ENABLE
static bool s_debug_uart_ready;
#endif

static int capture_log_vprintf(const char *format, va_list args)
{
    /* Preserve the local console, especially for startup and panic diagnostics. */
    int console_length = 0;
    if (s_console_vprintf != NULL) {
        va_list console_args;
        va_copy(console_args, args);
        console_length = s_console_vprintf(format, console_args);
        va_end(console_args);
    }

#if CONFIG_VOCAT_DEBUG_UART_ENABLE
    if (s_debug_uart_ready) {
        va_list uart_args;
        va_copy(uart_args, args);
        char uart_text[LOG_TEXT_CAPACITY];
        int uart_length = vsnprintf(uart_text, sizeof(uart_text), format, uart_args);
        va_end(uart_args);
        if (uart_length > 0) {
            size_t length = (size_t)uart_length;
            if (length >= sizeof(uart_text)) length = sizeof(uart_text) - 1;
            uart_write_bytes(CONFIG_VOCAT_DEBUG_UART_PORT, uart_text, length);
        }
    }
#endif

    if (s_log_queue == NULL) return console_length;

    log_record_t record = {0};
    va_list copy;
    va_copy(copy, args);
    int formatted_length = vsnprintf(record.text, sizeof(record.text), format, copy);
    va_end(copy);

    if (formatted_length < 0) return formatted_length;

    size_t length = (size_t)formatted_length;
    if (length >= sizeof(record.text)) {
        static const char suffix[] = "...[truncated]\n";
        length = sizeof(record.text) - 1;
        size_t suffix_length = sizeof(suffix) - 1;
        memcpy(record.text + length - suffix_length, suffix, suffix_length);
    }

    record.length = (uint16_t)length;
    if (xQueueSend(s_log_queue, &record, 0) != pdTRUE) {
        __atomic_add_fetch(&s_dropped_records, 1, __ATOMIC_RELAXED);
    }
    return s_console_vprintf != NULL ? console_length : formatted_length;
}

static bool read_peer(uint8_t mac[ESP_NOW_ETH_ALEN])
{
    bool known;
    portENTER_CRITICAL(&s_peer_lock);
    known = s_peer_known;
    if (known) memcpy(mac, s_peer_mac, ESP_NOW_ETH_ALEN);
    portEXIT_CRITICAL(&s_peer_lock);
    return known;
}

static void receive_packet(const esp_now_recv_info_t *info,
                           const uint8_t *data, int data_length)
{
    if (info == NULL || info->src_addr == NULL || data == NULL ||
        data_length < (int)offsetof(log_packet_t, payload) ||
        data_length > ESP_NOW_MAX_DATA_LEN) {
        return;
    }

    const log_packet_t *packet = (const log_packet_t *)data;
    if (packet->magic != ESPNOW_LOG_MAGIC ||
        packet->version != ESPNOW_LOG_VERSION || packet->type != PACKET_HELLO) {
        if (packet->magic == ESPNOW_LOG_MAGIC &&
            packet->version == ESPNOW_LOG_VERSION && packet->type == PACKET_ACK &&
            packet->sequence == s_waiting_ack_sequence) {
            xSemaphoreGive(s_ack_semaphore);
        }
        return;
    }

    portENTER_CRITICAL(&s_peer_lock);
    memcpy(s_peer_mac, info->src_addr, ESP_NOW_ETH_ALEN);
    s_peer_known = true;
    portEXIT_CRITICAL(&s_peer_lock);
    if (s_tx_task != NULL) xTaskNotifyGive(s_tx_task);
}

static void send_complete(const esp_now_send_info_t *tx_info,
                          esp_now_send_status_t status)
{
    (void)tx_info;
    s_last_tx_succeeded = status == ESP_NOW_SEND_SUCCESS;
    if (s_tx_result_semaphore != NULL) xSemaphoreGive(s_tx_result_semaphore);
}

static bool ensure_peer(const uint8_t mac[ESP_NOW_ETH_ALEN])
{
    if (esp_now_is_peer_exist(mac)) return true;

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
    peer.ifidx = WIFI_IF_STA;
    peer.channel = 0; /* Follow the STA channel shared with the access point. */
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

static bool send_packet_with_retry(const uint8_t mac[ESP_NOW_ETH_ALEN],
                                   const log_packet_t *packet, size_t packet_length)
{
    for (int attempt = 0; attempt < SEND_RETRIES; ++attempt) {
        xSemaphoreTake(s_tx_result_semaphore, 0);
        s_last_tx_succeeded = false;
        esp_err_t err = esp_now_send(mac, (const uint8_t *)packet, packet_length);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(80));
            continue;
        }
        if (xSemaphoreTake(s_tx_result_semaphore, pdMS_TO_TICKS(TX_WAIT_MS)) == pdTRUE &&
            s_last_tx_succeeded) {
            return true;
        }
    }
    return false;
}

static bool send_record(const uint8_t mac[ESP_NOW_ETH_ALEN],
                        const log_record_t *record, uint32_t sequence)
{
    uint8_t fragment_count = (uint8_t)((record->length + ESPNOW_LOG_PAYLOAD_MAX - 1) /
                                       ESPNOW_LOG_PAYLOAD_MAX);
    if (fragment_count == 0) fragment_count = 1;

    for (int attempt = 0; attempt < SEND_RETRIES; ++attempt) {
        s_waiting_ack_sequence = sequence;
        xSemaphoreTake(s_ack_semaphore, 0);

        bool packets_sent = true;
        for (uint8_t fragment = 0; fragment < fragment_count; ++fragment) {
            size_t offset = fragment * ESPNOW_LOG_PAYLOAD_MAX;
            size_t remaining = record->length - offset;
            size_t part_length = remaining < ESPNOW_LOG_PAYLOAD_MAX
                                     ? remaining
                                     : ESPNOW_LOG_PAYLOAD_MAX;
            log_packet_t packet = {
                .magic = ESPNOW_LOG_MAGIC,
                .version = ESPNOW_LOG_VERSION,
                .type = PACKET_LOG,
                .sequence = sequence,
                .fragment_index = fragment,
                .fragment_count = fragment_count,
                .payload_length = (uint16_t)part_length,
                .total_length = record->length,
            };
            memcpy(packet.payload, record->text + offset, part_length);
            if (!send_packet_with_retry(mac, &packet,
                                        offsetof(log_packet_t, payload) + part_length)) {
                packets_sent = false;
                break;
            }
        }

        if (packets_sent &&
            xSemaphoreTake(s_ack_semaphore, pdMS_TO_TICKS(ACK_WAIT_MS)) == pdTRUE) {
            return true;
        }
    }
    return false;
}

static void send_record_until_acked(uint8_t mac[ESP_NOW_ETH_ALEN],
                                    const log_record_t *record, uint32_t sequence)
{
    while (!send_record(mac, record, sequence)) {
        uint8_t updated_mac[ESP_NOW_ETH_ALEN];
        if (read_peer(updated_mac)) memcpy(mac, updated_mac, ESP_NOW_ETH_ALEN);
        if (!ensure_peer(mac)) vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void tx_task(void *arg)
{
    (void)arg;
    log_record_t record;
    for (;;) {
        if (xQueueReceive(s_log_queue, &record, portMAX_DELAY) != pdTRUE) continue;

        uint8_t mac[ESP_NOW_ETH_ALEN];
        while (!read_peer(mac)) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        }
        if (!ensure_peer(mac)) {
            xQueueSendToFront(s_log_queue, &record, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        uint32_t dropped = __atomic_exchange_n(&s_dropped_records, 0, __ATOMIC_RELAXED);
        if (dropped != 0) {
            log_record_t notice = {0};
            int n = snprintf(notice.text, sizeof(notice.text),
                             "[vocat_espnow_log] dropped %lu log record(s) because the transmit queue was full\n",
                             (unsigned long)dropped);
            notice.length = n > 0 ? (uint16_t)n : 0;
            if (notice.length != 0) {
                send_record_until_acked(mac, &notice, s_next_sequence++);
            }
        }

        send_record_until_acked(mac, &record, s_next_sequence++);
    }
}

esp_err_t vocat_espnow_log_init(void)
{
    if (s_log_queue != NULL) return ESP_OK;
    s_log_queue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(log_record_t));
    s_tx_result_semaphore = xSemaphoreCreateBinary();
    s_ack_semaphore = xSemaphoreCreateBinary();
    if (s_log_queue == NULL || s_tx_result_semaphore == NULL || s_ack_semaphore == NULL) {
        return ESP_ERR_NO_MEM;
    }

#if CONFIG_VOCAT_DEBUG_UART_ENABLE
    ESP_RETURN_ON_ERROR(uart_driver_install(CONFIG_VOCAT_DEBUG_UART_PORT, 256, 2048,
                                            0, NULL, 0),
                        TAG, "debug UART driver install failed");
    ESP_RETURN_ON_ERROR(uart_param_config(CONFIG_VOCAT_DEBUG_UART_PORT,
                                          &(uart_config_t){
                                              .baud_rate = CONFIG_VOCAT_DEBUG_UART_BAUDRATE,
                                              .data_bits = UART_DATA_8_BITS,
                                              .parity = UART_PARITY_DISABLE,
                                              .stop_bits = UART_STOP_BITS_1,
                                              .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                                              .source_clk = UART_SCLK_DEFAULT,
                                          }),
                        TAG, "debug UART configuration failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(CONFIG_VOCAT_DEBUG_UART_PORT,
                                     CONFIG_VOCAT_DEBUG_UART_TX_GPIO,
                                     CONFIG_VOCAT_DEBUG_UART_RX_GPIO,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        TAG, "debug UART pin setup failed");
    s_debug_uart_ready = true;
#endif

    s_console_vprintf = esp_log_set_vprintf(capture_log_vprintf);
    BaseType_t task_result = xTaskCreate(tx_task, "espnow_log_tx", 4096, NULL, 5,
                                         &s_tx_task);
    return task_result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t vocat_espnow_log_start(void)
{
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "ESP-NOW init failed");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(receive_packet), TAG,
                        "ESP-NOW receive callback registration failed");
    ESP_RETURN_ON_ERROR(esp_now_register_send_cb(send_complete), TAG,
                        "ESP-NOW send callback registration failed");
    return ESP_OK;
}
