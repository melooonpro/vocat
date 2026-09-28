/*
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Validate the ESP32-S3-WROOM-2-N32R16V flash and PSRAM capacities. */
esp_err_t vocat_bsp_memory_validate(void);

/** Initialize the ESP-VoCat power, QSPI bus, ST77916 panel and backlight. */
esp_err_t vocat_bsp_display_init(void);

/** Convert 8-bit RGB components to the byte order expected by the LCD. */
uint16_t vocat_bsp_rgb565(uint8_t red, uint8_t green, uint8_t blue);

/** Draw an RGB565 bitmap. Pixels must be produced by vocat_bsp_rgb565(). */
esp_err_t vocat_bsp_display_draw_bitmap(int x_start, int y_start, int x_end, int y_end,
                                        const uint16_t *pixels);

/** Draw a dark background and a centered, mint-green "Hello" label. */
esp_err_t vocat_bsp_display_show_hello(void);

/** Turn the LCD backlight fully on or off. */
esp_err_t vocat_bsp_display_set_backlight(bool enabled);

#ifdef __cplusplus
}
#endif
