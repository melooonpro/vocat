/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t air_mouse_init(void);
void air_mouse_set_active(bool active);
void air_mouse_set_scroll_active(bool active);
bool air_mouse_is_ready(void);
bool air_mouse_is_calibrating(void);
int8_t air_mouse_last_dx(void);
int8_t air_mouse_last_dy(void);
