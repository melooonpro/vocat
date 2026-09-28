/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "clock_ui.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_vocat_v1_1.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT    BIT1
#define WIFI_MAX_RETRIES   8

static const char *TAG = "vocat_clock";
static EventGroupHandle_t s_wifi_events;
static int s_wifi_retries;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        if (s_wifi_retries++ < WIFI_MAX_RETRIES) {
            ESP_LOGW(TAG, "Wi-Fi disconnected, retrying (%d/%d)",
                     s_wifi_retries, WIFI_MAX_RETRIES);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAILED_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "Wi-Fi connected, address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retries = 0;
        xEventGroupClearBits(s_wifi_events, WIFI_FAILED_BIT);
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_start_and_wait(void)
{
    if (CONFIG_VOCAT_WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "Wi-Fi SSID is empty; configure it in menuconfig");
        return false;
    }

    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) {
        ESP_LOGE(TAG, "Unable to allocate Wi-Fi event group");
        return false;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, CONFIG_VOCAT_WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, CONFIG_VOCAT_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = CONFIG_VOCAT_WIFI_PASSWORD[0]
                                             ? WIFI_AUTH_WPA2_PSK
                                             : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                            WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static bool system_time_is_valid(const struct tm *timeinfo)
{
    return timeinfo != NULL && timeinfo->tm_year >= (2024 - 1900);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP-VoCat v1.1 N32R16 flip clock");
    ESP_ERROR_CHECK(vocat_bsp_memory_validate());
    ESP_ERROR_CHECK(vocat_bsp_display_init());
    ESP_ERROR_CHECK(clock_ui_init());
    ESP_ERROR_CHECK(clock_ui_render(NULL, false, false));
    ESP_ERROR_CHECK(vocat_bsp_display_set_backlight(true));

    init_nvs();
    bool wifi_connected = wifi_start_and_wait();
    ESP_ERROR_CHECK(clock_ui_render(NULL, false, wifi_connected));

    if (wifi_connected) {
        setenv("TZ", CONFIG_VOCAT_TIMEZONE, 1);
        tzset();

        esp_sntp_config_t sntp_config =
            ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_VOCAT_NTP_SERVER);
        ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp_config));
        esp_err_t sync_result = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(20000));
        if (sync_result == ESP_OK) {
            ESP_LOGI(TAG, "System time synchronized by %s", CONFIG_VOCAT_NTP_SERVER);
        } else {
            ESP_LOGW(TAG, "NTP server %s timed out; trying ntp.aliyun.com",
                     CONFIG_VOCAT_NTP_SERVER);
            esp_netif_sntp_deinit();
            esp_sntp_config_t fallback_config =
                ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
            ESP_ERROR_CHECK(esp_netif_sntp_init(&fallback_config));
            sync_result = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(12000));
            if (sync_result == ESP_OK) {
                ESP_LOGI(TAG, "System time synchronized by ntp.aliyun.com");
            } else {
                ESP_LOGW(TAG, "NTP fallback timed out; background retries remain active");
            }
        }
    }

    struct tm previous = {0};
    bool previous_valid = false;
    bool previous_wifi = wifi_connected;

    while (true) {
        if (s_wifi_events != NULL) {
            wifi_connected = (xEventGroupGetBits(s_wifi_events) & WIFI_CONNECTED_BIT) != 0;
        }

        time_t now = time(NULL);
        struct tm current = {0};
        localtime_r(&now, &current);
        bool current_valid = system_time_is_valid(&current);
        bool time_changed = !previous_valid || current.tm_year != previous.tm_year ||
                            current.tm_yday != previous.tm_yday ||
                            current.tm_hour != previous.tm_hour ||
                            current.tm_min != previous.tm_min ||
                            current.tm_sec != previous.tm_sec;

        if (current_valid && previous_valid && time_changed) {
            ESP_ERROR_CHECK(clock_ui_animate(&previous, &current, true, wifi_connected));
        } else if (time_changed || current_valid != previous_valid ||
                   wifi_connected != previous_wifi) {
            ESP_ERROR_CHECK(clock_ui_render(current_valid ? &current : NULL,
                                             current_valid, wifi_connected));
        }

        if (current_valid) {
            previous = current;
            previous_valid = true;
        }
        previous_wifi = wifi_connected;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
