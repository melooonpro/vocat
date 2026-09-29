/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "esp_codec_dev.h"
#include "esp_err.h"

#define VOCAT_V10_MIC_SAMPLE_RATE 48000

/** Apply the VoCat v1.0 LCD power/reset sequence (LCD reset is GPIO3). */
esp_err_t vocat_v1_0_prepare_display(void);

/** Initialize ES7210 MIC1 using the VoCat v1.0 I2S_DI pin, GPIO15. */
esp_codec_dev_handle_t vocat_v1_0_microphone_init(void);
