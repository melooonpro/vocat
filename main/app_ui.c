/* SPDX-License-Identifier: Apache-2.0 */

#include "app_ui.h"
#include "air_mouse.h"
#include "bsp/esp_vocat.h"
#include "clock_ui.h"
#include "esp_check.h"
#include "esp_log.h"
#include "mouse_ui.h"
#include "voice_ui.h"
#include "voice_usb.h"
#include "vocat_v1_0.h"

static lv_obj_t *s_clock;
static lv_obj_t *s_voice;
static lv_obj_t *s_mouse;

typedef enum {
    APP_PAGE_CLOCK = 0,
    APP_PAGE_VOICE,
    APP_PAGE_MOUSE,
    APP_PAGE_COUNT,
} app_page_t;

static volatile app_page_t s_page = APP_PAGE_CLOCK;
static const char *TAG = "vocat_ui";
static const char *const s_page_names[] = {"Clock", "Mic", "Mouse"};

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

static void show_page(app_page_t page)
{
    if (page == s_page || page >= APP_PAGE_COUNT) return;
    ESP_LOGI(TAG, "Page %s -> %s", s_page_names[s_page], s_page_names[page]);

    if (s_page == APP_PAGE_VOICE) voice_ui_force_release();
    if (s_page == APP_PAGE_MOUSE) {
        mouse_ui_force_release();
        air_mouse_set_active(false);
    }

    voice_usb_set_page_active(page != APP_PAGE_CLOCK);
    if (page == APP_PAGE_MOUSE) air_mouse_set_active(true);

    lv_obj_t *pages[] = {s_clock, s_voice, s_mouse};
    for (int i = 0; i < APP_PAGE_COUNT; ++i) {
        slide(pages[i], lv_obj_get_x(pages[i]), (i - (int)page) * 360);
    }
    s_page = page;
}

static void gesture_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_GESTURE_LEFT && s_page + 1 < APP_PAGE_COUNT) {
        show_page((app_page_t)(s_page + 1));
    } else if (code == LV_EVENT_GESTURE_RIGHT && s_page > APP_PAGE_CLOCK) {
        show_page((app_page_t)(s_page - 1));
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
    lv_obj_set_scrollable(screen, false);
    s_clock = clock_ui_create(screen);
    s_voice = voice_ui_create(screen);
    s_mouse = mouse_ui_create(screen);
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

bool app_ui_is_clock_page(void)
{
    return s_page == APP_PAGE_CLOCK;
}
