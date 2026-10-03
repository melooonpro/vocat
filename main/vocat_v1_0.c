/* SPDX-License-Identifier: Apache-2.0 */

#include "vocat_v1_0.h"

#include "bsp/esp_vocat.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/touch_sens.h"
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "eaf_ui.h"

#define VOCAT_V10_LCD_POWER GPIO_NUM_9
#define VOCAT_V10_LCD_RESET GPIO_NUM_3
#define VOCAT_V10_I2S_MCLK  GPIO_NUM_42
#define VOCAT_V10_I2S_BCLK  GPIO_NUM_40
#define VOCAT_V10_I2S_WS    GPIO_NUM_39
#define VOCAT_V10_I2S_DOUT  GPIO_NUM_41
#define VOCAT_V10_I2S_DIN   GPIO_NUM_15
#define VOCAT_V10_TOP_TOUCH_CH 7
#define VOCAT_V10_TOP_TOUCH_GPIO GPIO_NUM_7
#define VOCAT_V10_TOUCH_THRESHOLD_RATIO 0.005f
#define VOCAT_V10_TOUCH_INIT_RETRIES 3
#define VOCAT_V10_TOUCH_LOG_INTERVAL_MS 500

static const char *TAG = "vocat_v1_0";
static const audio_codec_data_if_t *s_bsp_mic_data;
static bool s_mic_i2s_enabled;
static touch_sensor_handle_t s_top_touch_sensor;
static touch_channel_handle_t s_top_touch_channel;
static TaskHandle_t s_top_touch_log_task;
static bool s_top_touch_initialized;
static uint32_t s_top_touch_baseline;
static uint32_t s_top_touch_active_threshold;

static void top_touch_log_task(void *arg)
{
    (void)arg;
    while (s_top_touch_initialized) {
        uint32_t raw = 0;
        uint32_t smooth = 0;
        uint32_t benchmark = 0;
        esp_err_t raw_err = touch_channel_read_data(s_top_touch_channel,
                                                    TOUCH_CHAN_DATA_TYPE_RAW,
                                                    &raw);
        esp_err_t smooth_err = touch_channel_read_data(s_top_touch_channel,
                                                        TOUCH_CHAN_DATA_TYPE_SMOOTH,
                                                        &smooth);
        esp_err_t benchmark_err = touch_channel_read_data(
            s_top_touch_channel, TOUCH_CHAN_DATA_TYPE_BENCHMARK, &benchmark);
        if (raw_err != ESP_OK || smooth_err != ESP_OK || benchmark_err != ESP_OK) {
            ESP_LOGW(TAG, "Touch sample read failed: raw=%s smooth=%s benchmark=%s",
                     esp_err_to_name(raw_err), esp_err_to_name(smooth_err),
                     esp_err_to_name(benchmark_err));
        } else {
            int32_t delta = (int32_t)smooth - (int32_t)benchmark;
            int32_t baseline_delta = (int32_t)smooth - (int32_t)s_top_touch_baseline;
            ESP_LOGI(TAG,
                     "Touch GPIO%d CH%d: raw=%lu smooth=%lu benchmark=%lu delta=%ld baseline_delta=%ld threshold=%lu state=%s",
                     (int)VOCAT_V10_TOP_TOUCH_GPIO, VOCAT_V10_TOP_TOUCH_CH,
                     (unsigned long)raw, (unsigned long)smooth,
                     (unsigned long)benchmark, (long)delta, (long)baseline_delta,
                     (unsigned long)s_top_touch_active_threshold,
                     delta > (int32_t)s_top_touch_active_threshold ? "ACTIVE" : "idle");
        }
        vTaskDelay(pdMS_TO_TICKS(VOCAT_V10_TOUCH_LOG_INTERVAL_MS));
    }
    s_top_touch_log_task = NULL;
    vTaskDelete(NULL);
}

static bool top_touch_active_callback(touch_sensor_handle_t sensor,
                                      const touch_active_event_data_t *event,
                                      void *user_ctx)
{
    (void)sensor;
    (void)user_ctx;
    if (event != NULL && event->chan_id == VOCAT_V10_TOP_TOUCH_CH) {
        ESP_EARLY_LOGI(TAG, "Top touch active on channel %d", event->chan_id);
        /* The callback can run from the touch driver's ISR context. */
        eaf_ui_request_happy();
    }
    return false;
}

esp_err_t vocat_v1_0_top_touch_init(void)
{
    if (s_top_touch_initialized) return ESP_OK;

    touch_sensor_sample_config_t sample_cfgs[] = {
        TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5,
                                              TOUCH_VOLT_LIM_H_2V2),
    };
    touch_sensor_config_t sensor_cfg =
        TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(TOUCH_SAMPLE_CFG_NUM, sample_cfgs);
    ESP_RETURN_ON_ERROR(touch_sensor_new_controller(&sensor_cfg,
                                                    &s_top_touch_sensor),
                        TAG, "create top touch sensor");

    const touch_channel_config_t channel_cfg = {
        .active_thresh = {2000},
        .charge_speed = TOUCH_CHARGE_SPEED_7,
        .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
    };
    esp_err_t err = touch_sensor_new_channel(s_top_touch_sensor,
                                             VOCAT_V10_TOP_TOUCH_CH,
                                             &channel_cfg,
                                             &s_top_touch_channel);
    if (err != ESP_OK) {
        touch_sensor_del_controller(s_top_touch_sensor);
        s_top_touch_sensor = NULL;
        return err;
    }

    touch_chan_info_t channel_info = {0};
    err = touch_sensor_get_channel_info(s_top_touch_channel, &channel_info);
    if (err != ESP_OK) goto cleanup;
    ESP_LOGI(TAG, "Touch channel %d is mapped to GPIO%d",
             channel_info.chan_id, (int)channel_info.chan_gpio);
    if (channel_info.chan_gpio != VOCAT_V10_TOP_TOUCH_GPIO) {
        ESP_LOGE(TAG, "Touch channel %d is not GPIO%d (mapped to GPIO%d)",
                 VOCAT_V10_TOP_TOUCH_CH, (int)VOCAT_V10_TOP_TOUCH_GPIO,
                 (int)channel_info.chan_gpio);
        err = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    const touch_sensor_filter_config_t filter_cfg =
        TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    err = touch_sensor_config_filter(s_top_touch_sensor, &filter_cfg);
    if (err != ESP_OK) goto cleanup;
    err = touch_sensor_enable(s_top_touch_sensor);
    if (err != ESP_OK) goto cleanup;

    for (int i = 0; i < VOCAT_V10_TOUCH_INIT_RETRIES; ++i) {
        err = touch_sensor_trigger_oneshot_scanning(s_top_touch_sensor, 2000);
        if (err != ESP_OK) goto cleanup;
    }

    uint32_t data[TOUCH_SAMPLE_CFG_NUM];
    err = touch_channel_read_data(s_top_touch_channel,
                                  TOUCH_CHAN_DATA_TYPE_SMOOTH, data);
    if (err != ESP_OK) goto cleanup;
    const uint32_t baseline = data[0];
    if (baseline == 0) {
        ESP_LOGE(TAG, "Top touch channel returned a zero baseline");
        err = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }
    s_top_touch_baseline = baseline;
    ESP_LOGI(TAG, "Top touch baseline on channel %d: %lu",
             VOCAT_V10_TOP_TOUCH_CH, (unsigned long)baseline);

    /* The v2 touch driver uses a threshold relative to the measured
     * benchmark. Calibrate it from the actual board instead of relying on
     * the driver's generic 2000-count estimate. */
    err = touch_sensor_disable(s_top_touch_sensor);
    if (err != ESP_OK) goto cleanup;
    touch_channel_config_t calibrated_cfg = channel_cfg;
    calibrated_cfg.active_thresh[0] =
        (uint32_t)((float)baseline * VOCAT_V10_TOUCH_THRESHOLD_RATIO);
    if (calibrated_cfg.active_thresh[0] == 0) {
        calibrated_cfg.active_thresh[0] = 1;
    }
    s_top_touch_active_threshold = calibrated_cfg.active_thresh[0];
    err = touch_sensor_reconfig_channel(s_top_touch_channel, &calibrated_cfg);
    if (err != ESP_OK) goto cleanup;

    const touch_event_callbacks_t callbacks = {
        .on_active = top_touch_active_callback,
    };
    err = touch_sensor_register_callbacks(s_top_touch_sensor, &callbacks, NULL);
    if (err != ESP_OK) goto cleanup;
    err = touch_sensor_enable(s_top_touch_sensor);
    if (err != ESP_OK) goto cleanup;
    err = touch_sensor_start_continuous_scanning(s_top_touch_sensor);
    if (err != ESP_OK) goto cleanup;
    s_top_touch_initialized = true;
    if (xTaskCreate(top_touch_log_task, "touch_log", 3072, NULL, 2,
                    &s_top_touch_log_task) != pdPASS) {
        ESP_LOGE(TAG, "Unable to create touch logging task");
        err = ESP_ERR_NO_MEM;
        s_top_touch_initialized = false;
        goto cleanup;
    }
    ESP_LOGI(TAG, "Top touch pad initialized on channel %d (threshold %lu)",
             VOCAT_V10_TOP_TOUCH_CH,
             (unsigned long)calibrated_cfg.active_thresh[0]);
    return ESP_OK;

cleanup:
    s_top_touch_initialized = false;
    if (s_top_touch_log_task != NULL) {
        vTaskDelete(s_top_touch_log_task);
        s_top_touch_log_task = NULL;
    }
    touch_sensor_stop_continuous_scanning(s_top_touch_sensor);
    touch_sensor_disable(s_top_touch_sensor);
    if (s_top_touch_channel != NULL) {
        touch_sensor_del_channel(s_top_touch_channel);
        s_top_touch_channel = NULL;
    }
    touch_sensor_del_controller(s_top_touch_sensor);
    s_top_touch_sensor = NULL;
    return err;
}

static bool mic_data_is_open(const audio_codec_data_if_t *data)
{
    (void)data;
    return s_bsp_mic_data->is_open(s_bsp_mic_data);
}

static int mic_data_enable(const audio_codec_data_if_t *data,
                           esp_codec_dev_type_t type, bool enable)
{
    (void)data;
    if (type == ESP_CODEC_DEV_TYPE_IN && s_mic_i2s_enabled == enable) {
        return ESP_CODEC_DEV_OK;
    }
    int result = s_bsp_mic_data->enable(s_bsp_mic_data, type, enable);
    if (result == ESP_CODEC_DEV_OK && type == ESP_CODEC_DEV_TYPE_IN) {
        s_mic_i2s_enabled = enable;
    }
    return result;
}

static int mic_data_set_fmt(const audio_codec_data_if_t *data,
                            esp_codec_dev_type_t type,
                            esp_codec_dev_sample_info_t *format)
{
    (void)data;
    /* BSP already configured this exact format. Its codec data interface
     * unconditionally disables RX on every set_fmt, including after close. */
    if (type == ESP_CODEC_DEV_TYPE_IN &&
        format->sample_rate == VOCAT_V10_MIC_SAMPLE_RATE &&
        format->channel == 2 && format->bits_per_sample == 16 &&
        format->channel_mask == 0) {
        return ESP_CODEC_DEV_OK;
    }
    int result = s_bsp_mic_data->set_fmt(s_bsp_mic_data, type, format);
    if (result == ESP_CODEC_DEV_OK && type == ESP_CODEC_DEV_TYPE_IN) {
        s_mic_i2s_enabled = false;
    }
    return result;
}

static int mic_data_read(const audio_codec_data_if_t *data,
                         uint8_t *buffer, int size)
{
    (void)data;
    return s_bsp_mic_data->read(s_bsp_mic_data, buffer, size);
}

static const audio_codec_data_if_t s_mic_data = {
    .is_open = mic_data_is_open,
    .enable = mic_data_enable,
    .set_fmt = mic_data_set_fmt,
    .read = mic_data_read,
};

esp_err_t vocat_v1_0_prepare_display(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = BIT64(VOCAT_V10_LCD_POWER) | BIT64(VOCAT_V10_LCD_RESET),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "configure v1.0 LCD control");
    ESP_RETURN_ON_ERROR(gpio_set_level(VOCAT_V10_LCD_POWER, 0), TAG, "enable LCD power");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level(VOCAT_V10_LCD_RESET, 0), TAG, "assert LCD reset");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level(VOCAT_V10_LCD_RESET, 1), TAG, "release LCD reset");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

esp_codec_dev_handle_t vocat_v1_0_microphone_init(void)
{
    const i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(VOCAT_V10_MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = VOCAT_V10_I2S_MCLK,
            .bclk = VOCAT_V10_I2S_BCLK,
            .ws = VOCAT_V10_I2S_WS,
            .dout = VOCAT_V10_I2S_DOUT,
            .din = VOCAT_V10_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    if (bsp_i2c_init() != ESP_OK || bsp_audio_init(&i2s_config) != ESP_OK) return NULL;
    const audio_codec_data_if_t *data_if = bsp_audio_get_codec_itf();
    if (data_if == NULL) return NULL;
    s_bsp_mic_data = data_if;
    s_mic_i2s_enabled = true; /* bsp_audio_init enabled RX. */

    audio_codec_i2c_cfg_t i2c_config = {
        .port = BSP_I2C_NUM,
        .addr = ES7210_CODEC_DEFAULT_ADDR,
        .bus_handle = bsp_i2c_get_handle(),
    };
    const audio_codec_ctrl_if_t *control_if = audio_codec_new_i2c_ctrl(&i2c_config);
    if (control_if == NULL) return NULL;

    es7210_codec_cfg_t codec_config = {
        .ctrl_if = control_if,
        .master_mode = false,
        /* The VoCat mic FPC carries both ES7210 differential inputs. */
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
        .mclk_src = ES7210_MCLK_FROM_PAD,
        .mclk_div = 256,
    };
    const audio_codec_if_t *codec_if = es7210_codec_new(&codec_config);
    if (codec_if == NULL) return NULL;

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if,
        .data_if = &s_mic_data,
    };
    return esp_codec_dev_new(&device_config);
}
