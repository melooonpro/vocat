/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "sdkconfig.h"
#include "uac_config.h"
#include "uac_descriptors.h"
#include "tusb_config_uac.h"

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CONFIG_USB_HS 0

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined by the TinyUSB component
#endif
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_FREERTOS
#endif
#ifndef ESP_PLATFORM
#define ESP_PLATFORM 1
#endif
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif
#define CFG_TUSB_OS_INC_PATH freertos/
#define CFG_TUD_ENABLED 1
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_HID 1
#define CFG_TUD_HID_EP_BUFSIZE 16
