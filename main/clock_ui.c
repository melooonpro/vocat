/* SPDX-License-Identifier: Apache-2.0 */

#include "clock_ui.h"
#include <stdio.h>
#include "sdkconfig.h"

#define CARD_W 104
#define CARD_H 120
#define HALF_H 60
#define DIGIT_W 50
#define DIGIT_H 80
#define DIGIT_BYTES (DIGIT_W * DIGIT_H / 2)
#define DIGIT_CELL_W (CARD_W / 2)
#define FLAP_MIN_SCALE 24

typedef struct {
    lv_obj_t *root;
    lv_obj_t *base_top;
    lv_obj_t *base_bottom;
    lv_obj_t *bottom_mask;
    lv_obj_t *flap_top;
    lv_obj_t *flap_bottom;
    lv_obj_t *bottom_edge;
    lv_obj_t *images[4][2];
    int value;
    int next_value;
} flip_card_t;

static lv_obj_t *s_root;
static lv_obj_t *s_date;
static lv_obj_t *s_status;
static flip_card_t s_cards[3];
static lv_image_dsc_t s_digit_images[10];
static uint8_t s_left[10];
static uint8_t s_right[10];
static bool s_images_ready;

extern const uint8_t clock_digits_start[] asm("_binary_clock_digits_bin_start");

static lv_obj_t *make_half(flip_card_t *card, int layer, int y, lv_color_t color)
{
    lv_obj_t *half = lv_obj_create(card->root);
    lv_obj_set_size(half, CARD_W, HALF_H);
    lv_obj_set_pos(half, 0, y);
    lv_obj_set_style_bg_color(half, color, 0);
    lv_obj_set_style_bg_opa(half, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(half, 0, 0);
    lv_obj_set_style_radius(half, 12, 0);
    lv_obj_set_style_pad_all(half, 0, 0);
    lv_obj_remove_flag(half, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    for (int i = 0; i < 2; ++i) {
        card->images[layer][i] = lv_image_create(half);
        lv_obj_set_style_image_recolor(card->images[layer][i], lv_color_hex(0xd2d2d4), 0);
        lv_obj_set_style_image_recolor_opa(card->images[layer][i], LV_OPA_COVER, 0);
    }
    return half;
}

static void prepare_images(void)
{
    if (s_images_ready) return;
    for (int digit = 0; digit < 10; ++digit) {
        s_digit_images[digit] = (lv_image_dsc_t){
            .header = {
                .magic = LV_IMAGE_HEADER_MAGIC,
                .cf = LV_COLOR_FORMAT_A4,
                .w = DIGIT_W,
                .h = DIGIT_H,
                .stride = DIGIT_W / 2,
            },
            .data_size = DIGIT_BYTES,
            .data = clock_digits_start + digit * DIGIT_BYTES,
        };
        int left = DIGIT_W;
        int right = -1;
        for (int y = 0; y < DIGIT_H; ++y) {
            for (int x = 0; x < DIGIT_W; ++x) {
                uint8_t packed = s_digit_images[digit].data[y * (DIGIT_W / 2) + x / 2];
                uint8_t alpha = (x & 1) ? (packed & 0x0f) : (packed >> 4);
                if (alpha) {
                    if (x < left) left = x;
                    if (x > right) right = x;
                }
            }
        }
        s_left[digit] = left < DIGIT_W ? left : 0;
        s_right[digit] = right >= 0 ? right : DIGIT_W - 1;
    }
    s_images_ready = true;
}

static void set_half_value(flip_card_t *card, int layer, int value, bool bottom)
{
    int digits[2] = {value / 10, value % 10};
    int widths[2] = {
        s_right[digits[0]] - s_left[digits[0]] + 1,
        s_right[digits[1]] - s_left[digits[1]] + 1,
    };
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *image = card->images[layer][i];
        int cell_x = i * DIGIT_CELL_W;
        int glyph_x = cell_x + (DIGIT_CELL_W - widths[i]) / 2;
        lv_image_set_src(image, &s_digit_images[digits[i]]);
        lv_obj_set_pos(image, glyph_x - s_left[digits[i]], bottom ? -40 : 20);
    }
}

static void set_scale_y(void *object, int32_t value)
{
    lv_obj_set_style_transform_scale_y((lv_obj_t *)object, value, 0);
}

static void set_bottom_fold(void *object, int32_t value)
{
    lv_obj_t *flap = object;
    flip_card_t *card = lv_obj_get_user_data(lv_obj_get_parent(flap));
    int32_t covered_height = (HALF_H * value + 255) / 256;
    lv_obj_set_style_transform_scale_y(flap, value, 0);
    lv_obj_set_height(card->bottom_mask, covered_height);
    lv_obj_set_y(card->bottom_edge, HALF_H + covered_height - 2);
}

static void finish_top_fold(lv_anim_t *animation)
{
    lv_obj_add_flag((lv_obj_t *)animation->var, LV_OBJ_FLAG_HIDDEN);
}

static void start_bottom_fold(lv_anim_t *animation)
{
    lv_obj_t *flap = (lv_obj_t *)animation->var;
    flip_card_t *card = lv_obj_get_user_data(lv_obj_get_parent(flap));
    lv_obj_remove_flag(card->bottom_mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(card->flap_bottom, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(card->bottom_edge, LV_OBJ_FLAG_HIDDEN);
}

static void finish_flip(lv_anim_t *animation)
{
    lv_obj_t *flap = (lv_obj_t *)animation->var;
    flip_card_t *card = lv_obj_get_user_data(lv_obj_get_parent(flap));
    card->value = card->next_value;
    set_half_value(card, 0, card->value, false);
    set_half_value(card, 1, card->value, true);
    lv_obj_add_flag(card->flap_top, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(card->flap_bottom, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(card->bottom_mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(card->bottom_edge, LV_OBJ_FLAG_HIDDEN);
}

static void flip_to(flip_card_t *card, int value)
{
    if (card->value == value) return;
    lv_anim_delete(card->flap_top, set_scale_y);
    lv_anim_delete(card->flap_bottom, set_bottom_fold);
    card->next_value = value;
    set_half_value(card, 0, value, false);
    set_half_value(card, 1, card->value, true);
    set_half_value(card, 2, card->value, false);
    set_half_value(card, 3, value, true);
    lv_obj_remove_flag(card->flap_top, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(card->bottom_mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(card->flap_bottom, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(card->bottom_edge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_transform_scale_y(card->flap_top, 256, 0);
    lv_obj_set_style_transform_scale_y(card->flap_bottom, FLAP_MIN_SCALE, 0);
    lv_obj_set_height(card->bottom_mask, 1);
    lv_obj_set_y(card->bottom_edge, HALF_H);

    /* Ease-in gives the falling face increasing speed. The second face starts
       before the first reaches the hinge, avoiding the old mid-flip pause. */
    lv_anim_t top;
    lv_anim_init(&top);
    lv_anim_set_var(&top, card->flap_top);
    lv_anim_set_exec_cb(&top, set_scale_y);
    lv_anim_set_values(&top, 256, FLAP_MIN_SCALE);
    lv_anim_set_duration(&top, 190);
    lv_anim_set_path_cb(&top, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&top, finish_top_fold);
    lv_anim_start(&top);

    lv_anim_t bottom;
    lv_anim_init(&bottom);
    lv_anim_set_var(&bottom, card->flap_bottom);
    lv_anim_set_exec_cb(&bottom, set_bottom_fold);
    lv_anim_set_values(&bottom, FLAP_MIN_SCALE, 256);
    lv_anim_set_delay(&bottom, 145);
    lv_anim_set_duration(&bottom, 235);
    lv_anim_set_path_cb(&bottom, lv_anim_path_ease_out);
    lv_anim_set_start_cb(&bottom, start_bottom_fold);
    lv_anim_set_completed_cb(&bottom, finish_flip);
    lv_anim_start(&bottom);
}

static void init_card(flip_card_t *card, lv_obj_t *parent, int x)
{
    card->root = lv_obj_create(parent);
    lv_obj_set_size(card->root, CARD_W, CARD_H);
    lv_obj_set_pos(card->root, x, 105);
    lv_obj_set_style_pad_all(card->root, 0, 0);
    lv_obj_set_style_radius(card->root, 13, 0);
    lv_obj_set_style_bg_color(card->root, lv_color_hex(0x151517), 0);
    lv_obj_set_style_border_color(card->root, lv_color_hex(0x313136), 0);
    lv_obj_set_style_border_width(card->root, 1, 0);
    lv_obj_remove_flag(card->root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_user_data(card->root, card);
    card->base_top = make_half(card, 0, 0, lv_color_hex(0x1d1d20));
    card->base_bottom = make_half(card, 1, HALF_H, lv_color_hex(0x171719));
    card->bottom_mask = lv_obj_create(card->root);
    lv_obj_set_size(card->bottom_mask, CARD_W, 1);
    lv_obj_set_pos(card->bottom_mask, 0, HALF_H);
    lv_obj_set_style_bg_color(card->bottom_mask, lv_color_hex(0x171719), 0);
    lv_obj_set_style_bg_opa(card->bottom_mask, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card->bottom_mask, 0, 0);
    lv_obj_set_style_radius(card->bottom_mask, 0, 0);
    lv_obj_set_style_pad_all(card->bottom_mask, 0, 0);
    lv_obj_remove_flag(card->bottom_mask,
                       LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_flag(card->bottom_mask, LV_OBJ_FLAG_HIDDEN);
    card->flap_top = make_half(card, 2, 0, lv_color_hex(0x202023));
    card->flap_bottom = make_half(card, 3, HALF_H, lv_color_hex(0x18181a));
    card->bottom_edge = lv_obj_create(card->root);
    lv_obj_set_size(card->bottom_edge, CARD_W, 2);
    lv_obj_set_pos(card->bottom_edge, 0, HALF_H);
    lv_obj_set_style_bg_color(card->bottom_edge, lv_color_hex(0x080809), 0);
    lv_obj_set_style_bg_opa(card->bottom_edge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card->bottom_edge, 0, 0);
    lv_obj_set_style_radius(card->bottom_edge, 0, 0);
    lv_obj_set_style_pad_all(card->bottom_edge, 0, 0);
    lv_obj_remove_flag(card->bottom_edge,
                       LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_flag(card->bottom_edge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_transform_pivot_y(card->flap_top, HALF_H, 0);
    lv_obj_set_style_transform_pivot_y(card->flap_bottom, 0, 0);
    lv_obj_add_flag(card->flap_top, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(card->flap_bottom, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *hinge = lv_obj_create(card->root);
    lv_obj_set_size(hinge, CARD_W, 3);
    lv_obj_set_pos(hinge, 0, HALF_H - 1);
    lv_obj_set_style_bg_color(hinge, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hinge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hinge, 0, 0);
    lv_obj_set_style_radius(hinge, 0, 0);
    lv_obj_remove_flag(hinge, LV_OBJ_FLAG_SCROLLABLE);
    card->value = -1;
}

lv_obj_t *clock_ui_create(lv_obj_t *parent)
{
    prepare_images();
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, 360, 360);
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_root);
    lv_label_set_text(title, "VoCat Clock");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xeeeeef), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 42);
    init_card(&s_cards[0], s_root, 18);
    init_card(&s_cards[1], s_root, 128);
    init_card(&s_cards[2], s_root, 238);

    s_date = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_date, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_date, lv_color_hex(0xd0d0d2), 0);
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, 274);
    s_status = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x929298), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 314);
    return s_root;
}

void clock_ui_update(const struct tm *timeinfo, bool time_synced, bool wifi_connected)
{
    static const char *days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    if (!time_synced || timeinfo == NULL) {
        lv_label_set_text(s_date, "WAITING FOR TIME");
        lv_label_set_text(s_status, wifi_connected ? "SYNCING" :
                          (CONFIG_VOCAT_WIFI_SSID[0] ? "CONNECTING" : "SET WIFI"));
        return;
    }
    int hour = timeinfo->tm_hour;
#if !CONFIG_VOCAT_CLOCK_24_HOUR
    hour %= 12;
    if (hour == 0) hour = 12;
#endif
    int values[3] = {hour, timeinfo->tm_min, timeinfo->tm_sec};
    for (int i = 0; i < 3; ++i) {
        if (s_cards[i].value < 0) {
            s_cards[i].value = values[i];
            set_half_value(&s_cards[i], 0, values[i], false);
            set_half_value(&s_cards[i], 1, values[i], true);
        } else {
            flip_to(&s_cards[i], values[i]);
        }
    }
    char date[32];
    snprintf(date, sizeof(date), "%s  %04d-%02d-%02d", days[timeinfo->tm_wday],
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
    lv_label_set_text(s_date, date);
    lv_label_set_text(s_status, "NTP SYNC");
}
