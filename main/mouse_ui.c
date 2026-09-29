/* SPDX-License-Identifier: Apache-2.0 */

#include "mouse_ui.h"

#include <stdlib.h>
#include "air_mouse.h"
#include "esp_log.h"
#include "voice_usb.h"

static const char *TAG = "vocat_mouse_ui";

#define WHEEL_STEP_PIXELS 18
#define INTERACTION_TOP 88
#define CENTER_ZONE_HEIGHT 64
#define INTERACTION_HEIGHT (360 - CENTER_ZONE_HEIGHT - INTERACTION_TOP)
#define SIDE_ZONE_WIDTH 140
#define SCROLL_ZONE_WIDTH 80
#define CENTER_HOLD_MS 1000
#define CENTER_MOVE_TOLERANCE 14

typedef struct {
    uint8_t button;
    lv_obj_t *zone;
} mouse_button_context_t;

static lv_obj_t *s_status;
static lv_obj_t *s_wheel_indicator;
static lv_obj_t *s_center_zone;
static lv_obj_t *s_center_label;
static mouse_button_context_t s_left;
static mouse_button_context_t s_right;
static int s_wheel_last_y;
static int s_wheel_remainder;
static bool s_wheel_pressed;
static bool s_center_eligible;
static bool s_center_fired;
static lv_point_t s_center_press_point;
static uint32_t s_center_press_tick;

static void mouse_button_event(lv_event_t *event)
{
    mouse_button_context_t *context = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        voice_usb_mouse_set_button(context->button, true);
        ESP_LOGI(TAG, "%s button pressed", context->button == VOICE_USB_MOUSE_BUTTON_LEFT ? "Left" : "Right");
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        voice_usb_mouse_set_button(context->button, false);
        ESP_LOGI(TAG, "%s button released", context->button == VOICE_USB_MOUSE_BUTTON_LEFT ? "Left" : "Right");
    } else if (code == LV_EVENT_GESTURE || code == LV_EVENT_GESTURE_LEFT ||
               code == LV_EVENT_GESTURE_RIGHT) {
        /* Mouse interaction owns this area; page navigation stays in the title band. */
        lv_event_stop_bubbling(event);
    }
}

static void restore_center_visual(lv_timer_t *timer)
{
    (void)timer;
    lv_obj_set_style_border_color(s_center_zone, lv_color_white(), 0);
    lv_obj_set_style_text_color(s_center_label, lv_color_white(), 0);
}

static void center_cursor(void)
{
    voice_usb_mouse_center();
    ESP_LOGI(TAG, "Cursor center requested");
    lv_color_t active_color = lv_color_hex(0x28e878);
    lv_obj_set_style_border_color(s_center_zone, active_color, 0);
    lv_obj_set_style_text_color(s_center_label, active_color, 0);
    lv_timer_t *timer = lv_timer_create(restore_center_visual, 240, NULL);
    lv_timer_set_repeat_count(timer, 1);
}

static void wheel_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        s_wheel_last_y = point.y;
        s_wheel_remainder = 0;
        s_wheel_pressed = true;
        air_mouse_set_scroll_active(true);
        ESP_LOGI(TAG, "Scroll gesture started");
        lv_obj_set_style_bg_color(s_wheel_indicator, lv_color_hex(0x28e878), 0);
    } else if (code == LV_EVENT_PRESSING && s_wheel_pressed) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        s_wheel_remainder += s_wheel_last_y - point.y;
        s_wheel_last_y = point.y;
        int steps = s_wheel_remainder / WHEEL_STEP_PIXELS;
        if (steps != 0) {
            if (steps > 4) steps = 4;
            if (steps < -4) steps = -4;
            voice_usb_mouse_scroll(steps);
            s_wheel_remainder -= steps * WHEEL_STEP_PIXELS;
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_wheel_pressed) ESP_LOGI(TAG, "Scroll gesture ended");
        s_wheel_pressed = false;
        s_wheel_remainder = 0;
        air_mouse_set_scroll_active(false);
        lv_obj_set_style_bg_color(s_wheel_indicator, lv_color_white(), 0);
    } else if (code == LV_EVENT_GESTURE || code == LV_EVENT_GESTURE_LEFT ||
               code == LV_EVENT_GESTURE_RIGHT) {
        lv_event_stop_bubbling(event);
    }
}

static void center_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(lv_indev_active(), &s_center_press_point);
        s_center_press_tick = lv_tick_get();
        s_center_eligible = true;
        s_center_fired = false;
    } else if (code == LV_EVENT_PRESSING && s_center_eligible && !s_center_fired) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        int travel_x = point.x - s_center_press_point.x;
        int travel_y = point.y - s_center_press_point.y;
        if (travel_x < 0) travel_x = -travel_x;
        if (travel_y < 0) travel_y = -travel_y;
        if (travel_x > CENTER_MOVE_TOLERANCE ||
            travel_y > CENTER_MOVE_TOLERANCE) {
            s_center_eligible = false;
        } else if (lv_tick_elaps(s_center_press_tick) >= CENTER_HOLD_MS) {
            s_center_fired = true;
            center_cursor();
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_center_eligible = false;
    } else if (code == LV_EVENT_GESTURE || code == LV_EVENT_GESTURE_LEFT ||
               code == LV_EVENT_GESTURE_RIGHT) {
        lv_event_stop_bubbling(event);
    }
}

static void status_timer(lv_timer_t *timer)
{
    (void)timer;
    if (!air_mouse_is_ready()) {
        lv_label_set_text(s_status, "IMU connecting...");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0xffb84d), 0);
    } else if (air_mouse_is_calibrating()) {
        lv_label_set_text(s_status, "Hold still...");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0xffd166), 0);
    } else if (!voice_usb_is_mounted()) {
        lv_label_set_text(s_status, "USB connecting...");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0xffb84d), 0);
    } else {
        lv_label_set_text(s_status, "Ready");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0x5df29a), 0);
    }
}

static lv_obj_t *make_zone(lv_obj_t *parent, int x, int width)
{
    lv_obj_t *zone = lv_obj_create(parent);
    lv_obj_set_pos(zone, x, INTERACTION_TOP);
    lv_obj_set_size(zone, width, INTERACTION_HEIGHT);
    lv_obj_set_style_bg_opa(zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(zone, 0, 0);
    lv_obj_set_style_radius(zone, 0, 0);
    lv_obj_set_style_pad_all(zone, 0, 0);
    lv_obj_set_scrollable(zone, false);
    return zone;
}

lv_obj_t *mouse_ui_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 360, 360);
    lv_obj_set_pos(root, 720, 0);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_scrollable(root, false);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "VoCat Mouse");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xeeeeef), 0);
    /* Keep the title on the exact same baseline as "VoCat Mic". */
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 42);

    s_status = lv_label_create(root);
    lv_label_set_text(s_status, "IMU connecting...");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xffb84d), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 18);

    /* Match the microphone page exactly: a 4 px outer control ring and an
     * 8 px inner glyph. No wheel/split marker is drawn. */
    lv_obj_t *body = lv_obj_create(root);
    lv_obj_set_size(body, 176, 176);
    /* Same 176 px circle and center point as the microphone button. */
    lv_obj_align(body, LV_ALIGN_CENTER, 0, 4);
    lv_obj_set_style_radius(body, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(body, lv_color_white(), 0);
    lv_obj_set_style_border_width(body, 4, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_clickable(body, false);

    lv_obj_t *mouse_glyph = lv_obj_create(body);
    lv_obj_set_size(mouse_glyph, 94, 132);
    lv_obj_center(mouse_glyph);
    lv_obj_set_style_bg_opa(mouse_glyph, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mouse_glyph, 0, 0);
    lv_obj_set_style_pad_all(mouse_glyph, 0, 0);
    lv_obj_set_scrollable(mouse_glyph, false);
    lv_obj_set_clickable(mouse_glyph, false);

    /* Draw the two SVG paths as genuinely separate geometry. */
    lv_obj_t *top_arc = lv_arc_create(mouse_glyph);
    lv_obj_set_pos(top_arc, 0, 0);
    lv_obj_set_size(top_arc, 94, 94);
    lv_arc_set_bg_angles(top_arc, 180, 360);
    lv_obj_set_style_arc_width(top_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(top_arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_color(top_arc, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(top_arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(top_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(top_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_clickable(top_arc, false);

    lv_obj_t *bottom_arc = lv_arc_create(mouse_glyph);
    lv_obj_set_pos(bottom_arc, 0, 38);
    lv_obj_set_size(bottom_arc, 94, 94);
    lv_arc_set_bg_angles(bottom_arc, 0, 180);
    lv_obj_set_style_arc_width(bottom_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(bottom_arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_color(bottom_arc, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(bottom_arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(bottom_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bottom_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_clickable(bottom_arc, false);

    lv_obj_t *left_side = lv_obj_create(mouse_glyph);
    lv_obj_set_pos(left_side, 0, 54);
    lv_obj_set_size(left_side, 8, 32);
    lv_obj_set_style_bg_color(left_side, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(left_side, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(left_side, 0, 0);
    lv_obj_set_style_radius(left_side, 0, 0);
    lv_obj_set_style_pad_all(left_side, 0, 0);
    lv_obj_set_scrollable(left_side, false);
    lv_obj_set_clickable(left_side, false);

    lv_obj_t *right_side = lv_obj_create(mouse_glyph);
    lv_obj_set_pos(right_side, 86, 54);
    lv_obj_set_size(right_side, 8, 32);
    lv_obj_set_style_bg_color(right_side, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(right_side, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right_side, 0, 0);
    lv_obj_set_style_radius(right_side, 0, 0);
    lv_obj_set_style_pad_all(right_side, 0, 0);
    lv_obj_set_scrollable(right_side, false);
    lv_obj_set_clickable(right_side, false);

    s_wheel_indicator = lv_obj_create(mouse_glyph);
    lv_obj_set_size(s_wheel_indicator, 8, 34);
    lv_obj_align(s_wheel_indicator, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_set_style_bg_color(s_wheel_indicator, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_wheel_indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_wheel_indicator, 0, 0);
    lv_obj_set_style_radius(s_wheel_indicator, 0, 0);
    lv_obj_set_style_pad_all(s_wheel_indicator, 0, 0);
    lv_obj_set_scrollable(s_wheel_indicator, false);
    lv_obj_set_clickable(s_wheel_indicator, false);

    /* Same full-width separator treatment as the Mic page Enter region. */
    s_center_zone = lv_obj_create(root);
    lv_obj_set_size(s_center_zone, 360, CENTER_ZONE_HEIGHT);
    lv_obj_align(s_center_zone, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_center_zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_center_zone, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_center_zone, 4, 0);
    lv_obj_set_style_border_side(s_center_zone, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(s_center_zone, 0, 0);
    lv_obj_set_style_pad_all(s_center_zone, 0, 0);
    lv_obj_set_scrollable(s_center_zone, false);
    lv_obj_add_event_cb(s_center_zone, center_event, LV_EVENT_ALL, NULL);

    s_center_label = lv_label_create(s_center_zone);
    lv_label_set_text(s_center_label, "RECENTER");
    lv_obj_set_style_text_font(s_center_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_center_label, lv_color_white(), 0);
    lv_obj_center(s_center_label);
    lv_obj_set_clickable(s_center_label, false);

    s_left.button = VOICE_USB_MOUSE_BUTTON_LEFT;
    s_left.zone = make_zone(root, 0, SIDE_ZONE_WIDTH);
    lv_obj_add_event_cb(s_left.zone, mouse_button_event, LV_EVENT_ALL, &s_left);

    s_right.button = VOICE_USB_MOUSE_BUTTON_RIGHT;
    s_right.zone = make_zone(root, SIDE_ZONE_WIDTH + SCROLL_ZONE_WIDTH,
                             SIDE_ZONE_WIDTH);
    lv_obj_add_event_cb(s_right.zone, mouse_button_event, LV_EVENT_ALL, &s_right);

    lv_obj_t *wheel_zone = make_zone(root, SIDE_ZONE_WIDTH, SCROLL_ZONE_WIDTH);
    lv_obj_add_event_cb(wheel_zone, wheel_event, LV_EVENT_ALL, NULL);

    lv_timer_create(status_timer, 150, NULL);
    return root;
}

void mouse_ui_force_release(void)
{
    voice_usb_mouse_release_all();
    s_wheel_pressed = false;
    s_center_eligible = false;
    s_center_fired = false;
    s_wheel_remainder = 0;
    air_mouse_set_scroll_active(false);
    if (s_wheel_indicator != NULL) {
        lv_obj_set_style_bg_color(s_wheel_indicator, lv_color_white(), 0);
    }
}
