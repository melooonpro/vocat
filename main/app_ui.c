/* SPDX-License-Identifier: Apache-2.0 */

#include "app_ui.h"
#include "bsp/esp_vocat.h"
#include "clock_ui.h"
#include "esp_check.h"
#include "voice_ui.h"
#include "voice_usb.h"
#include "vocat_v1_0.h"

static lv_obj_t *s_clock;
static lv_obj_t *s_voice;
static bool s_voice_visible;

static void set_x(void *object, int32_t value) { lv_obj_set_x((lv_obj_t *)object, value); }

static void slide(lv_obj_t *object, int from, int to)
{
    lv_anim_delete(object, set_x);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_exec_cb(&animation, set_x);
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_duration(&animation, 260);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static void gesture_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_GESTURE_LEFT && !s_voice_visible) {
        s_voice_visible = true;
        voice_usb_set_page_active(true);
        slide(s_clock, 0, -360);
        slide(s_voice, 360, 0);
    } else if (code == LV_EVENT_GESTURE_RIGHT && s_voice_visible) {
        voice_ui_force_release();
        voice_usb_set_page_active(false);
        s_voice_visible = false;
        slide(s_clock, -360, 0);
        slide(s_voice, 0, 360);
    }
}

esp_err_t app_ui_init(void)
{
    ESP_RETURN_ON_ERROR(vocat_v1_0_prepare_display(), "app_ui", "prepare v1.0 display");
    if (bsp_display_start() == NULL) return ESP_FAIL;
    if (!bsp_display_lock(0)) return ESP_ERR_TIMEOUT;
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    s_clock = clock_ui_create(screen);
    s_voice = voice_ui_create(screen);
    lv_obj_add_event_cb(screen, gesture_event, LV_EVENT_GESTURE_LEFT, NULL);
    lv_obj_add_event_cb(screen, gesture_event, LV_EVENT_GESTURE_RIGHT, NULL);
    bsp_display_unlock();
    return bsp_display_brightness_set(100);
}

void app_ui_update_clock(const struct tm *timeinfo, bool time_synced, bool wifi_connected)
{
    if (!bsp_display_lock(100)) return;
    clock_ui_update(timeinfo, time_synced, wifi_connected);
    bsp_display_unlock();
}
