/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define VOICE_USB_MOUSE_BUTTON_LEFT  0x01
#define VOICE_USB_MOUSE_BUTTON_RIGHT 0x02

esp_err_t voice_usb_init(void);
void voice_usb_set_page_active(bool active);
void voice_usb_set_pressed(bool pressed);
void voice_usb_send_enter(void);
void voice_usb_mouse_set_button(uint8_t button_mask, bool pressed);
void voice_usb_mouse_move(int dx, int dy);
void voice_usb_mouse_scroll(int steps);
void voice_usb_mouse_center(void);
void voice_usb_mouse_release_all(void);
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
