/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "esp_err.h"

/* Install the log capture queue before the first application log is emitted. */
esp_err_t vocat_espnow_log_init(void);

/* Start ESP-NOW after the station has connected to the configured 2.4 GHz AP. */
esp_err_t vocat_espnow_log_start(void);
