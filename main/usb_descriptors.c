/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include "tusb.h"
#include "uac_descriptors.h"

enum {
    ITF_NUM_AUDIO_CONTROL = 0,
    ITF_NUM_AUDIO_STREAMING_MIC,
    ITF_NUM_HID_KEYBOARD,
    ITF_NUM_TOTAL,
};

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_AUDIO,
    STRID_MICROPHONE,
    STRID_KEYBOARD,
};

#define EPNUM_AUDIO_IN 0x81
#define EPNUM_HID_IN   0x82
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_AUDIO_DEVICE_DESC_LEN + TUD_HID_DESC_LEN)

static uint8_t const s_hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

static tusb_desc_device_t const s_device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4017,
    .bcdDevice = 0x0101,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&s_device_descriptor;
}

static uint8_t const s_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 250),
    TUD_AUDIO_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, STRID_AUDIO,
                         0x01, EPNUM_AUDIO_IN, 0x83),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_KEYBOARD, STRID_KEYBOARD,
                       HID_ITF_PROTOCOL_KEYBOARD, sizeof(s_hid_report_descriptor),
                       EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 10),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return s_configuration_descriptor;
}

static char const *s_strings[] = {
    (const char[]){0x09, 0x04},
    "VoCat",
    "VoCat Mic",
    "VOCAT-V10",
    "VoCat Audio",
    "VoCat Microphone",
    "VoCat Voice Shortcut",
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_hid_report_descriptor;
}

static uint16_t s_string_descriptor[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    uint8_t count;
    if (index == 0) {
        memcpy(&s_string_descriptor[1], s_strings[0], 2);
        count = 1;
    } else {
        if (index >= sizeof(s_strings) / sizeof(s_strings[0])) return NULL;
        const char *text = s_strings[index];
        count = (uint8_t)strlen(text);
        if (count > 31) count = 31;
        for (uint8_t i = 0; i < count; ++i) s_string_descriptor[1 + i] = text[i];
    }
    s_string_descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
    return s_string_descriptor;
}
