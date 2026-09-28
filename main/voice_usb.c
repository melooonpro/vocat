/* SPDX-License-Identifier: Apache-2.0 */

#include "voice_usb.h"

#include <stdatomic.h>
#include <string.h>
#include "bsp/esp_vocat.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "hal/usb_wrap_ll.h"
#include "soc/usb_wrap_struct.h"
#include "tusb.h"
#include "usb_device_uac.h"
#include "vocat_v1_0.h"

#define SAMPLE_RATE 48000
#define UAC_INTERVAL_MS 10
#define UAC_MONO_SAMPLES ((SAMPLE_RATE / 1000) * UAC_INTERVAL_MS)

static const char *TAG = "vocat_mic";
static esp_codec_dev_handle_t s_microphone;
static atomic_bool s_mounted;
static atomic_bool s_page_requested;
static atomic_bool s_usb_active;
static atomic_bool s_pressed;
static atomic_uchar s_level;
static atomic_uchar s_level_left;
static atomic_uchar s_level_right;
static atomic_uchar s_waveform[VOICE_USB_WAVE_BARS];
static atomic_uint_fast32_t s_packet_count;
static atomic_uint_fast32_t s_read_error_count;
static int16_t s_stereo_buffer[UAC_MONO_SAMPLES * 2];
static bool s_codec_open;
static bool s_hid_down;
static bool s_usb_stack_initialized;

static uint8_t peak_to_level(uint32_t peak)
{
    return (uint8_t)((peak * 255U) / 32767U);
}

static void clear_audio_state(void)
{
    atomic_store(&s_level, 0);
    atomic_store(&s_level_left, 0);
    atomic_store(&s_level_right, 0);
    for (unsigned i = 0; i < VOICE_USB_WAVE_BARS; ++i) {
        atomic_store(&s_waveform[i], 0);
    }
}

static esp_err_t microphone_input(uint8_t *buf, size_t len, size_t *bytes_read, void *ctx)
{
    (void)ctx;
    *bytes_read = 0;
    atomic_fetch_add(&s_packet_count, 1);
    if (!atomic_load(&s_usb_active)) {
        memset(buf, 0, len);
        *bytes_read = len;
        return ESP_OK;
    }
    if (s_microphone == NULL || (len & 1U) || len > UAC_MONO_SAMPLES * sizeof(int16_t)) {
        atomic_fetch_add(&s_read_error_count, 1);
        return ESP_ERR_INVALID_STATE;
    }

    /* Read both ES7210 slots. Never sum them: opposite phase or a wrong slot
     * must not cancel an otherwise valid signal. The louder 10 ms slot is
     * forwarded to the host while both raw peaks stay visible on screen. */
    const size_t frames = len / sizeof(int16_t);
    const size_t stereo_bytes = frames * 2U * sizeof(int16_t);
    if (esp_codec_dev_read(s_microphone, s_stereo_buffer, (int)stereo_bytes) != ESP_CODEC_DEV_OK) {
        atomic_fetch_add(&s_read_error_count, 1);
        memset(buf, 0, len);
        *bytes_read = len;
        return ESP_OK;
    }

    uint32_t peak_left = 0;
    uint32_t peak_right = 0;
    for (size_t i = 0; i < frames; ++i) {
        int32_t left = s_stereo_buffer[i * 2U];
        int32_t right = s_stereo_buffer[i * 2U + 1U];
        uint32_t left_magnitude = left < 0 ? (uint32_t)-left : (uint32_t)left;
        uint32_t right_magnitude = right < 0 ? (uint32_t)-right : (uint32_t)right;
        if (left_magnitude > peak_left) peak_left = left_magnitude;
        if (right_magnitude > peak_right) peak_right = right_magnitude;
    }
    const unsigned selected_slot = peak_right > peak_left ? 1U : 0U;
    int16_t *mono = (int16_t *)buf;
    uint32_t wave_peaks[VOICE_USB_WAVE_BARS] = {0};
    for (size_t i = 0; i < frames; ++i) {
        int32_t sample = s_stereo_buffer[i * 2U + selected_slot];
        mono[i] = (int16_t)sample;
        uint32_t magnitude = sample < 0 ? (uint32_t)-sample : (uint32_t)sample;
        unsigned bucket = (unsigned)((i * VOICE_USB_WAVE_BARS) / frames);
        if (bucket >= VOICE_USB_WAVE_BARS) bucket = VOICE_USB_WAVE_BARS - 1;
        if (magnitude > wave_peaks[bucket]) wave_peaks[bucket] = magnitude;
    }
    atomic_store(&s_level_left, peak_to_level(peak_left));
    atomic_store(&s_level_right, peak_to_level(peak_right));
    atomic_store(&s_level, peak_to_level(peak_left > peak_right ? peak_left : peak_right));
    if (atomic_load(&s_pressed)) {
        for (unsigned i = 0; i < VOICE_USB_WAVE_BARS; ++i) {
            atomic_store(&s_waveform[i], peak_to_level(wave_peaks[i]));
        }
    } else {
        for (unsigned i = 0; i < VOICE_USB_WAVE_BARS; ++i) {
            atomic_store(&s_waveform[i], 0);
        }
    }
    *bytes_read = len;
    return ESP_OK;
}

static esp_err_t usb_start(void)
{
    if (s_microphone == NULL) {
        s_microphone = vocat_v1_0_microphone_init();
        if (s_microphone == NULL) return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t format = {
        .sample_rate = SAMPLE_RATE,
        .channel = 2,
        .bits_per_sample = 16,
    };
    if (esp_codec_dev_open(s_microphone, &format) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    s_codec_open = true;
    esp_codec_dev_set_in_gain(s_microphone, 30.0f);

    if (!s_usb_stack_initialized) {
        uac_device_config_t config = {
            .skip_tinyusb_init = false,
            .input_cb = microphone_input,
            .output_cb = NULL,
            .set_mute_cb = NULL,
            .set_volume_cb = NULL,
            .cb_ctx = NULL,
            .spk_itf_num = -1,
            .mic_itf_num = 1,
        };
        esp_err_t err = uac_device_init(&config);
        if (err != ESP_OK) {
            esp_codec_dev_close(s_microphone);
            s_codec_open = false;
            return err;
        }
        s_usb_stack_initialized = true;
    } else {
        /* Give the shared internal PHY back to the already-running USB-OTG
         * stack and force a clean host re-enumeration. */
        usb_wrap_ll_phy_enable_external(&USB_WRAP, false);
        usb_wrap_ll_phy_enable_pad(&USB_WRAP, true);
        tud_connect();
    }
    atomic_store(&s_usb_active, true);
    ESP_LOGI(TAG, "Voice page USB enabled: UAC microphone + Ctrl/Win HID");
    return ESP_OK;
}

static void send_hid_state(bool down)
{
    if (!tud_hid_ready()) return;
    uint8_t modifier = down ? (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_LEFTGUI) : 0;
    if (tud_hid_keyboard_report(0, modifier, NULL)) {
        s_hid_down = down;
    }
}

static void usb_stop(void)
{
    atomic_store(&s_pressed, false);
    if (s_hid_down) {
        send_hid_state(false);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(20));
    if (s_codec_open) {
        esp_codec_dev_close(s_microphone);
        s_codec_open = false;
    }
    atomic_store(&s_mounted, false);
    atomic_store(&s_usb_active, false);
    s_hid_down = false;
    clear_audio_state();

    /* The ESP32-S3 internal PHY is shared by USB-OTG and USB Serial/JTAG. */
    usb_serial_jtag_ll_phy_enable_external(false);
    usb_serial_jtag_ll_phy_enable_pad(true);
    ESP_LOGI(TAG, "Voice page USB detached; USB Serial/JTAG restored");
}

static void usb_manager_task(void *ctx)
{
    (void)ctx;
    while (true) {
        bool requested = atomic_load(&s_page_requested);
        bool active = atomic_load(&s_usb_active);
        if (requested && !active) {
            if (usb_start() != ESP_OK) {
                ESP_LOGE(TAG, "Unable to enable voice USB");
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
        } else if (!requested && active) {
            usb_stop();
        }

        if (atomic_load(&s_usb_active)) {
            bool want_hid_down = atomic_load(&s_pressed) && atomic_load(&s_mounted);
            if (want_hid_down != s_hid_down) send_hid_state(want_hid_down);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t voice_usb_init(void)
{
    BaseType_t result = xTaskCreatePinnedToCore(usb_manager_task, "voice_usb", 4096, NULL,
                                                6, NULL, tskNO_AFFINITY);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void voice_usb_set_page_active(bool active)
{
    if (!active) atomic_store(&s_pressed, false);
    atomic_store(&s_page_requested, active);
}

void voice_usb_set_pressed(bool pressed)
{
    atomic_store(&s_pressed, pressed && atomic_load(&s_usb_active));
    if (!pressed) clear_audio_state();
}

bool voice_usb_is_mounted(void) { return atomic_load(&s_mounted); }
bool voice_usb_is_active(void) { return atomic_load(&s_usb_active); }
bool voice_usb_is_pressed(void) { return atomic_load(&s_pressed); }
uint8_t voice_usb_audio_level(void) { return atomic_load(&s_level); }
uint8_t voice_usb_waveform(unsigned index)
{
    return index < VOICE_USB_WAVE_BARS ? atomic_load(&s_waveform[index]) : 0;
}
uint8_t voice_usb_audio_level_left(void) { return atomic_load(&s_level_left); }
uint8_t voice_usb_audio_level_right(void) { return atomic_load(&s_level_right); }
uint32_t voice_usb_packet_count(void) { return (uint32_t)atomic_load(&s_packet_count); }
uint32_t voice_usb_read_error_count(void) { return (uint32_t)atomic_load(&s_read_error_count); }

void tud_mount_cb(void) { atomic_store(&s_mounted, true); }
void tud_umount_cb(void)
{
    atomic_store(&s_mounted, false);
    atomic_store(&s_level, 0);
    atomic_store(&s_level_left, 0);
    atomic_store(&s_level_right, 0);
}
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    atomic_store(&s_level, 0);
}
void tud_resume_cb(void) {}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t requested_length)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)requested_length;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t buffer_size)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)buffer_size;
}
