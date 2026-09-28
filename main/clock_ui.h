/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

esp_err_t clock_ui_init(void);
esp_err_t clock_ui_render(const struct tm *timeinfo, bool time_synced, bool wifi_connected);
esp_err_t clock_ui_animate(const struct tm *old_time, const struct tm *new_time,
                           bool time_synced, bool wifi_connected);
