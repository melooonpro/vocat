/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77916.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_vocat_v1_1.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "vocat_lcd_init_data.h"

static const char *TAG = "vocat_bsp";

#define VOCAT_LCD_H_RES             360
#define VOCAT_LCD_V_RES             360
#define VOCAT_LCD_HOST              SPI2_HOST
#define VOCAT_LCD_PIXEL_CLOCK_HZ     (40 * 1000 * 1000)
#define VOCAT_LCD_POWER_GPIO        GPIO_NUM_9
#define VOCAT_LCD_RESET_V10_GPIO    GPIO_NUM_3
#define VOCAT_LCD_RESET_V12_GPIO    GPIO_NUM_47
#define VOCAT_LCD_BACKLIGHT_GPIO    GPIO_NUM_44
#define VOCAT_LCD_PCLK_GPIO         GPIO_NUM_18
#define VOCAT_LCD_DATA0_GPIO        GPIO_NUM_46
#define VOCAT_LCD_DATA1_GPIO        GPIO_NUM_13
#define VOCAT_LCD_DATA2_GPIO        GPIO_NUM_11
#define VOCAT_LCD_DATA3_GPIO        GPIO_NUM_12
#define VOCAT_LCD_DC_GPIO           GPIO_NUM_45
#define VOCAT_LCD_CS_GPIO           GPIO_NUM_14
#define VOCAT_DRAW_ROWS             16
#define VOCAT_FONT_SCALE            10
#define VOCAT_FLASH_SIZE_BYTES      (32U * 1024U * 1024U)
#define VOCAT_PSRAM_SIZE_BYTES      (16U * 1024U * 1024U)

static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_transfer_done;

esp_err_t vocat_bsp_memory_validate(void)
{
    uint32_t flash_size = 0;
    ESP_RETURN_ON_ERROR(esp_flash_get_size(NULL, &flash_size), TAG, "read flash size");

    const size_t psram_size = esp_psram_get_size();
    ESP_LOGI(TAG, "ESP32-S3-WROOM-2-N32R16V: flash=%" PRIu32 " MiB, PSRAM=%u MiB",
             flash_size / (1024U * 1024U), (unsigned)(psram_size / (1024U * 1024U)));

    ESP_RETURN_ON_FALSE(flash_size == VOCAT_FLASH_SIZE_BYTES, ESP_ERR_INVALID_SIZE, TAG,
                        "expected 32 MiB octal flash, detected %" PRIu32 " bytes", flash_size);
    ESP_RETURN_ON_FALSE(psram_size == VOCAT_PSRAM_SIZE_BYTES, ESP_ERR_INVALID_SIZE, TAG,
                        "expected 16 MiB octal PSRAM, detected %u bytes", (unsigned)psram_size);
    return ESP_OK;
}

/* Five 5x7 glyphs, one byte per row, MSB-aligned. */
static const uint8_t s_hello_glyphs[5][7] = {
    {0x88, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88}, /* H */
    {0x00, 0x00, 0x70, 0x88, 0xF8, 0x80, 0x78}, /* e */
    {0xC0, 0x40, 0x40, 0x40, 0x40, 0x48, 0x30}, /* l */
    {0xC0, 0x40, 0x40, 0x40, 0x40, 0x48, 0x30}, /* l */
    {0x00, 0x00, 0x70, 0x88, 0x88, 0x88, 0x70}, /* o */
};

uint16_t vocat_bsp_rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    const uint16_t rgb565 = ((uint16_t)(red & 0xF8) << 8) |
                            ((uint16_t)(green & 0xFC) << 3) |
                            ((uint16_t)blue >> 3);
    return __builtin_bswap16(rgb565);
}

static bool color_transfer_done(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *event_data,
                                void *user_ctx)
{
    (void)panel_io;
    (void)event_data;
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_ctx, &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}

static esp_err_t draw_and_wait(int x_start, int y_start, int x_end, int y_end, const uint16_t *pixels)
{
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(s_panel, x_start, y_start, x_end, y_end, pixels),
                        TAG, "draw bitmap");
    if (xSemaphoreTake(s_transfer_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "LCD transfer timeout");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t vocat_bsp_display_draw_bitmap(int x_start, int y_start, int x_end, int y_end,
                                        const uint16_t *pixels)
{
    ESP_RETURN_ON_FALSE(s_panel != NULL, ESP_ERR_INVALID_STATE, TAG, "display is not initialized");
    ESP_RETURN_ON_FALSE(pixels != NULL, ESP_ERR_INVALID_ARG, TAG, "pixel buffer is null");
    ESP_RETURN_ON_FALSE(x_start >= 0 && y_start >= 0 && x_end <= VOCAT_LCD_H_RES &&
                        y_end <= VOCAT_LCD_V_RES && x_start < x_end && y_start < y_end,
                        ESP_ERR_INVALID_ARG, TAG, "invalid draw area");
    return draw_and_wait(x_start, y_start, x_end, y_end, pixels);
}

static esp_err_t configure_power_and_compatible_reset(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = BIT64(VOCAT_LCD_POWER_GPIO) |
                        BIT64(VOCAT_LCD_RESET_V10_GPIO) |
                        BIT64(VOCAT_LCD_RESET_V12_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "configure LCD control pins");

    /* LCD/SD power enable is active low on both published revisions. */
    ESP_RETURN_ON_ERROR(gpio_set_level(VOCAT_LCD_POWER_GPIO, 0), TAG, "enable LCD power");
    vTaskDelay(pdMS_TO_TICKS(20));

    /* v1.0: GPIO3 active-low reset. v1.2: GPIO47 active-high reset. */
    ESP_RETURN_ON_ERROR(gpio_set_level(VOCAT_LCD_RESET_V10_GPIO, 0), TAG, "assert legacy reset");
    ESP_RETURN_ON_ERROR(gpio_set_level(VOCAT_LCD_RESET_V12_GPIO, 1), TAG, "assert current reset");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level(VOCAT_LCD_RESET_V10_GPIO, 1), TAG, "release legacy reset");
    ESP_RETURN_ON_ERROR(gpio_set_level(VOCAT_LCD_RESET_V12_GPIO, 0), TAG, "release current reset");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

esp_err_t vocat_bsp_display_set_backlight(bool enabled)
{
    return gpio_set_level(VOCAT_LCD_BACKLIGHT_GPIO, enabled ? 1 : 0);
}

esp_err_t vocat_bsp_display_init(void)
{
    if (s_panel != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(configure_power_and_compatible_reset(), TAG, "power/reset display");

    const gpio_config_t backlight_config = {
        .pin_bit_mask = BIT64(VOCAT_LCD_BACKLIGHT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&backlight_config), TAG, "configure backlight");
    ESP_RETURN_ON_ERROR(vocat_bsp_display_set_backlight(false), TAG, "disable backlight");

    s_transfer_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_transfer_done != NULL, ESP_ERR_NO_MEM, TAG, "create transfer semaphore");

    const spi_bus_config_t bus_config = {
        .sclk_io_num = VOCAT_LCD_PCLK_GPIO,
        .data0_io_num = VOCAT_LCD_DATA0_GPIO,
        .data1_io_num = VOCAT_LCD_DATA1_GPIO,
        .data2_io_num = VOCAT_LCD_DATA2_GPIO,
        .data3_io_num = VOCAT_LCD_DATA3_GPIO,
        /* The centered HELLO bitmap is larger than a background stripe. */
        .max_transfer_sz = VOCAT_LCD_H_RES * 7 * VOCAT_FONT_SCALE * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(VOCAT_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        TAG, "initialize LCD QSPI bus");

    esp_lcd_panel_io_handle_t panel_io = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = VOCAT_LCD_DC_GPIO,
        .cs_gpio_num = VOCAT_LCD_CS_GPIO,
        .pclk_hz = VOCAT_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 1,
        .on_color_trans_done = color_transfer_done,
        .user_ctx = s_transfer_done,
        .flags.quad_mode = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)VOCAT_LCD_HOST,
                                                &io_config, &panel_io),
                        TAG, "create LCD panel IO");

    const st77916_vendor_config_t vendor_config = {
        .init_cmds = vocat_lcd_init_data,
        .init_cmds_size = sizeof(vocat_lcd_init_data) / sizeof(vocat_lcd_init_data[0]),
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        /* Reset is handled above to support the undocumented v1.1 marking. */
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st77916(panel_io, &panel_config, &s_panel),
                        TAG, "create ST77916 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "initialize ST77916 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "set panel inversion");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, false), TAG, "set panel axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, false, false), TAG, "set panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "enable panel output");

    ESP_LOGI(TAG, "ST77916 initialized at %dx%d", VOCAT_LCD_H_RES, VOCAT_LCD_V_RES);
    return ESP_OK;
}

esp_err_t vocat_bsp_display_show_hello(void)
{
    ESP_RETURN_ON_FALSE(s_panel != NULL, ESP_ERR_INVALID_STATE, TAG, "display is not initialized");

    const uint16_t background = vocat_bsp_rgb565(7, 17, 31);
    uint16_t *stripe = heap_caps_malloc(VOCAT_LCD_H_RES * VOCAT_DRAW_ROWS * sizeof(uint16_t),
                                        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(stripe != NULL, ESP_ERR_NO_MEM, TAG, "allocate stripe buffer");
    for (size_t i = 0; i < VOCAT_LCD_H_RES * VOCAT_DRAW_ROWS; ++i) {
        stripe[i] = background;
    }
    for (int y = 0; y < VOCAT_LCD_V_RES; y += VOCAT_DRAW_ROWS) {
        const int rows = (y + VOCAT_DRAW_ROWS <= VOCAT_LCD_V_RES) ?
                         VOCAT_DRAW_ROWS : (VOCAT_LCD_V_RES - y);
        esp_err_t ret = draw_and_wait(0, y, VOCAT_LCD_H_RES, y + rows, stripe);
        if (ret != ESP_OK) {
            free(stripe);
            return ret;
        }
    }
    free(stripe);

    const int glyph_width = 5 * VOCAT_FONT_SCALE;
    const int glyph_height = 7 * VOCAT_FONT_SCALE;
    const int spacing = VOCAT_FONT_SCALE;
    const int text_width = 5 * glyph_width + 4 * spacing;
    uint16_t *text = heap_caps_malloc(text_width * glyph_height * sizeof(uint16_t),
                                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(text != NULL, ESP_ERR_NO_MEM, TAG, "allocate text buffer");
    for (size_t i = 0; i < (size_t)text_width * glyph_height; ++i) {
        text[i] = background;
    }

    const uint16_t foreground = vocat_bsp_rgb565(94, 234, 212);
    for (int glyph = 0; glyph < 5; ++glyph) {
        const int glyph_x = glyph * (glyph_width + spacing);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((s_hello_glyphs[glyph][row] & (0x80 >> col)) == 0) {
                    continue;
                }
                for (int sy = 0; sy < VOCAT_FONT_SCALE; ++sy) {
                    for (int sx = 0; sx < VOCAT_FONT_SCALE; ++sx) {
                        const int x = glyph_x + col * VOCAT_FONT_SCALE + sx;
                        const int y = row * VOCAT_FONT_SCALE + sy;
                        text[y * text_width + x] = foreground;
                    }
                }
            }
        }
    }

    const int x = (VOCAT_LCD_H_RES - text_width) / 2;
    const int y = (VOCAT_LCD_V_RES - glyph_height) / 2;
    const esp_err_t ret = draw_and_wait(x, y, x + text_width, y + glyph_height, text);
    free(text);
    return ret;
}
