/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include "tusb.h"
#include "uac_descriptors.h"
#include "usb_hid_ids.h"

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
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(VOCAT_HID_REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(VOCAT_HID_REPORT_ID_MOUSE)),

    /* Absolute pointer used only by the hold-to-center control. Relative
     * reports above remain responsible for normal air-mouse movement. */
    0x05, 0x01,                         /* Usage Page (Generic Desktop) */
    0x09, 0x02,                         /* Usage (Mouse) */
    0xA1, 0x01,                         /* Collection (Application) */
    0x85, VOCAT_HID_REPORT_ID_ABSOLUTE_MOUSE,
    0x09, 0x01,                         /* Usage (Pointer) */
    0xA1, 0x00,                         /* Collection (Physical) */
    0x05, 0x09,                         /* Usage Page (Button) */
    0x19, 0x01,                         /* Usage Minimum (1) */
    0x29, 0x03,                         /* Usage Maximum (3) */
    0x15, 0x00,                         /* Logical Minimum (0) */
    0x25, 0x01,                         /* Logical Maximum (1) */
    0x95, 0x03,                         /* Report Count (3) */
    0x75, 0x01,                         /* Report Size (1) */
    0x81, 0x02,                         /* Input (Data, Variable, Absolute) */
    0x95, 0x01,                         /* Report Count (1) */
    0x75, 0x05,                         /* Report Size (5) */
    0x81, 0x01,                         /* Input (Constant) */
    0x05, 0x01,                         /* Usage Page (Generic Desktop) */
    0x09, 0x30,                         /* Usage (X) */
    0x09, 0x31,                         /* Usage (Y) */
    0x16, 0x00, 0x00,                   /* Logical Minimum (0) */
    0x26, 0xFF, 0x7F,                   /* Logical Maximum (32767) */
    0x75, 0x10,                         /* Report Size (16) */
    0x95, 0x02,                         /* Report Count (2) */
    0x81, 0x02,                         /* Input (Data, Variable, Absolute) */
    0xC0,                               /* End Collection */
    0xC0,                               /* End Collection */
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
    /* Bump the revision when the HID report descriptor changes so Windows
     * does not reuse the former keyboard-only descriptor from its cache. */
    .bcdDevice = 0x0103,
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
                       HID_ITF_PROTOCOL_NONE, sizeof(s_hid_report_descriptor),
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
    "VoCat Keyboard and Mouse",
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
