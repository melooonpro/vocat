/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "lvgl.h"

lv_obj_t *mouse_ui_create(lv_obj_t *parent);
void mouse_ui_force_release(void);
