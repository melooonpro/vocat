/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <time.h>
#include "lvgl.h"

lv_obj_t *clock_ui_create(lv_obj_t *parent);
void clock_ui_update(const struct tm *timeinfo, bool time_synced, bool wifi_connected);
