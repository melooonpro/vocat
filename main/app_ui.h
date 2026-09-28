/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

esp_err_t app_ui_init(void);
void app_ui_update_clock(const struct tm *timeinfo, bool time_synced, bool wifi_connected);
bool app_ui_is_clock_page(void);
