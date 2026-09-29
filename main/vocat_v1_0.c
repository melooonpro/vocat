/* SPDX-License-Identifier: Apache-2.0 */

#include "vocat_v1_0.h"

#include "bsp/esp_vocat.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define VOCAT_V10_LCD_POWER GPIO_NUM_9
#define VOCAT_V10_LCD_RESET GPIO_NUM_3
#define VOCAT_V10_I2S_MCLK  GPIO_NUM_42
#define VOCAT_V10_I2S_BCLK  GPIO_NUM_40
#define VOCAT_V10_I2S_WS    GPIO_NUM_39
#define VOCAT_V10_I2S_DOUT  GPIO_NUM_41
#define VOCAT_V10_I2S_DIN   GPIO_NUM_15

static const char *TAG = "vocat_v1_0";
static const audio_codec_data_if_t *s_bsp_mic_data;
static bool s_mic_i2s_enabled;

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
