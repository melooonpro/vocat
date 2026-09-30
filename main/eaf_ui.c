/* SPDX-License-Identifier: Apache-2.0 */

#include "eaf_ui.h"
#include "esp_log.h"
#include "lv_eaf.h"

#define EAF_FRAME_DELAY_MS 50

extern const uint8_t eaf_start[] asm("_binary_xiaohei_blink_360_eaf_start");
extern const uint8_t eaf_end[] asm("_binary_xiaohei_blink_360_eaf_end");

static lv_obj_t *s_animation;

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
    lv_eaf_set_src_data(s_animation, eaf_start, (size_t)(eaf_end - eaf_start));
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
    return root;
}

void eaf_ui_set_active(bool active)
{
    if (s_animation == NULL || !lv_eaf_is_loaded(s_animation)) return;
    if (active) lv_eaf_resume(s_animation);
    else lv_eaf_pause(s_animation);
}
