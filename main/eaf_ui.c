/* SPDX-License-Identifier: Apache-2.0 */

#include "eaf_ui.h"
#include "esp_log.h"
#include "lv_eaf.h"

#define EAF_FRAME_DELAY_MS 50
#define EAF_REQUEST_POLL_MS 20

extern const uint8_t blink_eaf_start[] asm("_binary_xiaohei_blink_360_eaf_start");
extern const uint8_t blink_eaf_end[] asm("_binary_xiaohei_blink_360_eaf_end");
extern const uint8_t happy_eaf_start[] asm("_binary_xiaohei_happy_360_eaf_start");
extern const uint8_t happy_eaf_end[] asm("_binary_xiaohei_happy_360_eaf_end");

static lv_obj_t *s_animation;
static bool s_active = true;
static bool s_happy_playing;
static bool s_restore_blink;
static volatile bool s_happy_requested;

static void set_blink_animation(void)
{
    lv_eaf_set_src_data(s_animation, blink_eaf_start,
                        (size_t)(blink_eaf_end - blink_eaf_start));
    if (!lv_eaf_is_loaded(s_animation)) {
        ESP_LOGE("eaf_ui", "Unable to load xiaohei_blink_360.eaf");
        s_happy_playing = false;
        return;
    }

    lv_eaf_set_loop_count(s_animation, -1);
    lv_eaf_set_loop_enabled(s_animation, true);
    lv_eaf_set_frame_delay(s_animation, EAF_FRAME_DELAY_MS);
    lv_eaf_resume(s_animation);
    s_happy_playing = false;
}

static void set_happy_animation(void)
{
    lv_eaf_set_src_data(s_animation, happy_eaf_start,
                        (size_t)(happy_eaf_end - happy_eaf_start));
    if (!lv_eaf_is_loaded(s_animation)) {
        ESP_LOGE("eaf_ui", "Unable to load xiaohei_happy_360.eaf");
        set_blink_animation();
        return;
    }

    /* Play happy once.  The READY event requests the switch back to blink. */
    lv_eaf_set_loop_count(s_animation, 0);
    lv_eaf_set_loop_enabled(s_animation, false);
    lv_eaf_set_frame_delay(s_animation, EAF_FRAME_DELAY_MS);
    s_happy_playing = true;
}

static void animation_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_READY && s_happy_playing) {
        /* lv_eaf pauses its timer after dispatching READY.  Defer the source
         * change to the polling timer so the new blink timer is not paused by
         * the finishing happy timer callback. */
        s_restore_blink = true;
    }
}

static void request_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    /* Restore first.  A touch arriving on the same tick should then start a
     * fresh happy animation instead of being immediately overwritten by the
     * stale READY flag. */
    if (s_restore_blink && s_active) {
        s_restore_blink = false;
        if (s_happy_playing && s_animation != NULL &&
            lv_eaf_is_loaded(s_animation)) {
            set_blink_animation();
        } else {
            s_happy_playing = false;
        }
    }

    if (s_happy_requested) {
        s_happy_requested = false;
        if (s_active && s_animation != NULL && lv_eaf_is_loaded(s_animation)) {
            set_happy_animation();
        }
    }
}

lv_obj_t *eaf_ui_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 360, 360);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_gesture_bubble(root, true);

    s_animation = lv_eaf_create(root);
    lv_obj_set_clickable(s_animation, false);
    lv_obj_set_scrollable(s_animation, false);
    lv_obj_add_event_cb(s_animation, animation_event, LV_EVENT_READY, NULL);
    lv_eaf_set_src_data(s_animation, blink_eaf_start,
                        (size_t)(blink_eaf_end - blink_eaf_start));
    if (lv_eaf_is_loaded(s_animation)) {
        lv_eaf_set_loop_count(s_animation, -1);
        lv_eaf_set_loop_enabled(s_animation, true);
        lv_eaf_set_frame_delay(s_animation, EAF_FRAME_DELAY_MS);
        lv_obj_center(s_animation);
        ESP_LOGI("eaf_ui", "EAF loaded: %ld frames", (long)lv_eaf_get_total_frames(s_animation));
    } else {
        ESP_LOGE("eaf_ui", "Unable to load xiaohei_blink_360.eaf");
        lv_obj_t *label = lv_label_create(root);
        lv_label_set_text(label, "Animation unavailable");
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_center(label);
    }
    lv_timer_create(request_timer_cb, EAF_REQUEST_POLL_MS, NULL);
    return root;
}

void eaf_ui_set_active(bool active)
{
    s_active = active;
    if (s_animation == NULL || !lv_eaf_is_loaded(s_animation)) return;
    if (active) lv_eaf_resume(s_animation);
    else lv_eaf_pause(s_animation);
}

void eaf_ui_request_happy(void)
{
    /* This function is called by the BSP button timer task.  Do not call LVGL
     * here; request_timer_cb consumes the flag from the LVGL task. */
    s_happy_requested = true;
}
