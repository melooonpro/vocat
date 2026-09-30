/* SPDX-License-Identifier: Apache-2.0 */

#include "voice_ui.h"
#include "voice_usb.h"
#include "esp_log.h"

static const char *TAG = "vocat_mic_ui";

static lv_obj_t *s_button;
static lv_obj_t *s_enter_zone;
static lv_obj_t *s_enter_arrow;
#define RIPPLE_COUNT 3
#define ENTER_DOUBLE_TAP_MS 550
#define ENTER_TAP_MOVE_TOLERANCE 36
static lv_obj_t *s_glow;
static lv_obj_t *s_ripples[RIPPLE_COUNT];
static bool s_pressed;
static bool s_enter_press_valid;
static bool s_enter_tap_waiting;
static lv_point_t s_enter_press_point;
static uint32_t s_last_enter_tap;
static uint16_t s_glow_phase;
static uint16_t s_level_smooth;

static void restore_enter_color(lv_timer_t *timer)
{
    (void)timer;
    lv_obj_set_style_border_color(s_enter_zone, lv_color_white(), 0);
    lv_obj_set_style_line_color(s_enter_arrow, lv_color_white(), 0);
}

static void send_enter(void)
{
    voice_usb_send_enter();
    ESP_LOGI(TAG, "Double tap: Enter requested");
    lv_color_t active_color = lv_color_hex(0x28e878);
    lv_obj_set_style_border_color(s_enter_zone, active_color, 0);
    lv_obj_set_style_line_color(s_enter_arrow, active_color, 0);
    lv_timer_t *timer = lv_timer_create(restore_enter_color, 160, NULL);
    lv_timer_set_repeat_count(timer, 1);
}

static void enter_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(lv_indev_active(), &s_enter_press_point);
        s_enter_press_valid = true;
    } else if (code == LV_EVENT_RELEASED && s_enter_press_valid) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        int dx = point.x - s_enter_press_point.x;
        int dy = point.y - s_enter_press_point.y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        s_enter_press_valid = false;

        /* Count taps ourselves instead of relying on LVGL's stricter
         * DOUBLE_CLICKED event. The complete area below the rule is active. */
        if (dx <= ENTER_TAP_MOVE_TOLERANCE && dy <= ENTER_TAP_MOVE_TOLERANCE) {
            uint32_t now = lv_tick_get();
            if (s_enter_tap_waiting &&
                lv_tick_diff(now, s_last_enter_tap) <= ENTER_DOUBLE_TAP_MS) {
                s_enter_tap_waiting = false;
                send_enter();
            } else {
                s_enter_tap_waiting = true;
                s_last_enter_tap = now;
            }
        }
    } else if (code == LV_EVENT_PRESS_LOST) {
        s_enter_press_valid = false;
    } else if (code == LV_EVENT_GESTURE || code == LV_EVENT_GESTURE_LEFT ||
               code == LV_EVENT_GESTURE_RIGHT) {
        lv_event_stop_bubbling(event);
    }
}

static void hide_glow(void)
{
    lv_obj_set_style_shadow_opa(s_glow, LV_OPA_TRANSP, 0);
    for (unsigned i = 0; i < RIPPLE_COUNT; ++i) {
        lv_obj_set_style_border_opa(s_ripples[i], LV_OPA_TRANSP, 0);
    }
}

static void set_pressed(bool pressed)
{
    if (s_pressed == pressed) return;
    s_pressed = pressed;
    voice_usb_set_pressed(pressed);
    ESP_LOGI(TAG, "Mic button %s", pressed ? "pressed" : "released");
    if (pressed) {
        s_glow_phase = 0;
        s_level_smooth = 0;
        lv_obj_set_style_bg_opa(s_button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(s_button, lv_color_white(), 0);
        lv_obj_set_style_shadow_opa(s_glow, 72, 0);
    } else {
        lv_obj_set_style_bg_opa(s_button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(s_button, lv_color_white(), 0);
        hide_glow();
    }
}

static void button_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) set_pressed(true);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) set_pressed(false);
}

static void status_timer(lv_timer_t *timer)
{
    (void)timer;
    if (s_pressed) {
        uint16_t target = voice_usb_audio_level();
        if (target > 120) target = 120;

        /* Fast attack and slow release turn the raw samples into a natural
         * envelope instead of visibly stepping between microphone values. */
        if (target > s_level_smooth) {
            s_level_smooth += (target - s_level_smooth + 1) / 2;
        } else {
            s_level_smooth -= (s_level_smooth - target + 7) / 8;
        }

        s_glow_phase = (s_glow_phase + 5) & 0xff;
        uint16_t breath = s_glow_phase < 128 ? s_glow_phase : 255 - s_glow_phase;
        unsigned energy = s_level_smooth;
        unsigned glow_opa = 58 + breath / 4 + energy;
        if (glow_opa > 210) glow_opa = 210;
        lv_obj_set_style_shadow_opa(s_glow, (lv_opa_t)glow_opa, 0);

        /* Three staggered wave fronts continuously travel from the button to
         * the display edge.  Their brightness and thickness follow speech. */
        for (unsigned i = 0; i < RIPPLE_COUNT; ++i) {
            unsigned progress = (s_glow_phase + i * (256 / RIPPLE_COUNT)) & 0xff;
            int size = 184 + (int)(progress * 172 / 255);
            unsigned fade = 255 - progress;
            unsigned opacity = 10 + fade * (38 + energy / 2) / 255;
            lv_obj_set_size(s_ripples[i], size, size);
            lv_obj_align(s_ripples[i], LV_ALIGN_CENTER, 0, 4);
            lv_obj_set_style_border_width(s_ripples[i], 10 + energy / 10, 0);
            lv_obj_set_style_border_opa(s_ripples[i], (lv_opa_t)opacity, 0);
        }
    }
}

static lv_obj_t *plain_rect(lv_obj_t *parent, int w, int h, int x, int y, int radius)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_set_size(object, w, h);
    lv_obj_align(object, LV_ALIGN_CENTER, x, y);
    lv_obj_set_style_bg_color(object, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_scrollable(object, false);
    lv_obj_set_clickable(object, false);
    return object;
}

lv_obj_t *voice_ui_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 360, 360);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_scrollable(root, false);

    /* A real LVGL shadow provides a continuous full-screen gradient. */
    s_glow = lv_obj_create(root);
    lv_obj_set_size(s_glow, 176, 176);
    lv_obj_align(s_glow, LV_ALIGN_CENTER, 0, 4);
    lv_obj_set_style_radius(s_glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_glow, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_glow, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_glow, 0, 0);
    lv_obj_set_style_shadow_color(s_glow, lv_color_hex(0x28e878), 0);
    lv_obj_set_style_shadow_width(s_glow, 112, 0);
    lv_obj_set_style_shadow_spread(s_glow, 8, 0);
    lv_obj_set_style_shadow_opa(s_glow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_glow, 0, 0);
    lv_obj_set_scrollable(s_glow, false);
    lv_obj_set_clickable(s_glow, false);

    /* Subtle expanding rings make the gradient feel alive and show speech
     * transients without changing the size of the actual button. */
    for (unsigned i = 0; i < RIPPLE_COUNT; ++i) {
        s_ripples[i] = lv_obj_create(root);
        lv_obj_set_size(s_ripples[i], 184, 184);
        lv_obj_align(s_ripples[i], LV_ALIGN_CENTER, 0, 4);
        lv_obj_set_style_radius(s_ripples[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_ripples[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(s_ripples[i], lv_color_hex(0x4cff91), 0);
        lv_obj_set_style_border_width(s_ripples[i], 10, 0);
        lv_obj_set_style_border_opa(s_ripples[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(s_ripples[i], 0, 0);
        lv_obj_set_scrollable(s_ripples[i], false);
        lv_obj_set_clickable(s_ripples[i], false);
    }

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "VoCat Mic");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xeeeeef), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 42);

    s_button = lv_obj_create(root);
    lv_obj_set_size(s_button, 176, 176);
    lv_obj_align(s_button, LV_ALIGN_CENTER, 0, 4);
    lv_obj_set_style_radius(s_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_button, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_button, 4, 0);
    lv_obj_set_style_pad_all(s_button, 0, 0);
    lv_obj_set_scrollable(s_button, false);
    lv_obj_add_event_cb(s_button, button_event, LV_EVENT_ALL, NULL);

    /* The former SVG is now drawn entirely from LVGL primitives. */
    /* SVG path 5483, scaled from its 384 x 640 outer capsule with a
     * 64-unit stroke.  The 50 x 84 result keeps the original 3:5 ratio. */
    lv_obj_t *capsule = lv_obj_create(s_button);
    lv_obj_set_size(capsule, 50, 84);
    lv_obj_align(capsule, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_radius(capsule, 25, 0);
    lv_obj_set_style_bg_opa(capsule, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(capsule, 8, 0);
    lv_obj_set_style_border_color(capsule, lv_color_white(), 0);
    lv_obj_set_style_pad_all(capsule, 0, 0);
    lv_obj_set_scrollable(capsule, false);
    lv_obj_set_clickable(capsule, false);

    /* SVG path 5484 is a single 352-radius lower semicircle.  It has no
     * separate vertical side bars; the rounded arc ends are the two tips. */
    lv_obj_t *receiver = lv_arc_create(s_button);
    lv_obj_set_size(receiver, 92, 92);
    lv_obj_align(receiver, LV_ALIGN_CENTER, 0, -5);
    lv_arc_set_bg_angles(receiver, 0, 180);
    lv_obj_set_style_arc_width(receiver, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(receiver, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(receiver, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(receiver, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(receiver, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(receiver, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_clickable(receiver, false);
    plain_rect(s_button, 8, 17, 0, 43, 4);
    plain_rect(s_button, 48, 8, 0, 55, 4);

    /* Everything below this full-width rule is the Enter hit target. */
    s_enter_zone = lv_obj_create(root);
    lv_obj_set_size(s_enter_zone, 360, 64);
    lv_obj_align(s_enter_zone, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_enter_zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_enter_zone, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_enter_zone, 4, 0);
    lv_obj_set_style_border_side(s_enter_zone, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(s_enter_zone, 0, 0);
    lv_obj_set_style_pad_all(s_enter_zone, 0, 0);
    lv_obj_set_scrollable(s_enter_zone, false);
    lv_obj_add_event_cb(s_enter_zone, enter_event, LV_EVENT_ALL, NULL);

    /* A single clean return arrow, centered below the separator. */
    static lv_point_precise_t arrow_points[] = {
        {74, 4}, {74, 26}, {18, 26},
        {32, 12}, {18, 26}, {32, 40},
    };
    lv_obj_t *icon = lv_obj_create(s_enter_zone);
    lv_obj_set_size(icon, 92, 44);
    lv_obj_center(icon);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_set_style_pad_all(icon, 0, 0);
    lv_obj_set_scrollable(icon, false);
    lv_obj_set_clickable(icon, false);

    s_enter_arrow = lv_line_create(icon);
    lv_line_set_points(s_enter_arrow, arrow_points,
                       sizeof(arrow_points) / sizeof(arrow_points[0]));
    lv_obj_set_style_line_color(s_enter_arrow, lv_color_white(), 0);
    lv_obj_set_style_line_width(s_enter_arrow, 4, 0);
    lv_obj_set_style_line_rounded(s_enter_arrow, true, 0);
    lv_obj_set_clickable(s_enter_arrow, false);

    lv_timer_create(status_timer, 33, NULL);
    return root;
}

void voice_ui_force_release(void)
{
    set_pressed(false);
    s_enter_press_valid = false;
    s_enter_tap_waiting = false;
}
