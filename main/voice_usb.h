/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t voice_usb_init(void);
void voice_usb_set_page_active(bool active);
void voice_usb_set_pressed(bool pressed);
bool voice_usb_is_mounted(void);
bool voice_usb_is_active(void);
bool voice_usb_is_pressed(void);
uint8_t voice_usb_audio_level(void);
uint8_t voice_usb_waveform(unsigned index);
#define VOICE_USB_WAVE_BARS 9
uint8_t voice_usb_audio_level_left(void);
uint8_t voice_usb_audio_level_right(void);
uint32_t voice_usb_packet_count(void);
uint32_t voice_usb_read_error_count(void);
