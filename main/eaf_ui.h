/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include "lvgl.h"

lv_obj_t *eaf_ui_create(lv_obj_t *parent);
void eaf_ui_set_active(bool active);
/** Request a one-shot happy animation from a non-LVGL input callback. */
void eaf_ui_request_happy(void);
