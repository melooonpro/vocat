/* SPDX-License-Identifier: Apache-2.0 */

#include "clock_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_vocat_v1_1.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define SCREEN_W 360
#define SCREEN_H 360
#define STRIPE_H 60
#define CARD_Y 105
#define CARD_W 104
#define CARD_H 120
#define DIGIT_W 50
#define DIGIT_H 80
#define DIGIT_Y (CARD_Y + (CARD_H - DIGIT_H) / 2)
#define FLIP_Y (DIGIT_Y + DIGIT_H / 2)
#define DIGIT_ROW_BYTES (DIGIT_W / 2)
#define DIGIT_GAP 2
#define ANIMATION_FRAMES 16
#define ANIMATION_FRAME_MS 25

typedef struct {
    int digits[6];
    int old_digits[6];
    int digit_x[6];
    int old_digit_x[6];
    int animation_step;
    uint8_t animated_cards;
    int16_t row_sample_y[CARD_H];
    uint8_t row_uses_old[CARD_H];
    uint8_t row_light[CARD_H];
    uint8_t row_is_edge[CARD_H];
    struct tm timeinfo;
    bool time_synced;
    bool wifi_connected;
    char date[40];
    char status[20];
} clock_scene_t;

static uint16_t *s_stripe;
static uint8_t s_digit_left[10];
static uint8_t s_digit_right[10];

static const int s_card_x[3] = {18, 128, 238};
/*
 * Signed vertical projection of a page rotating from 0 to 180 degrees.
 * The non-linear spacing models a page released with a little initial speed
 * and then accelerated by gravity.  Positive values are the falling old top
 * face; negative values are the new bottom face.  Crossing zero only once
 * removes the artificial pause at the hinge.
 */
static const int16_t s_flip_projection[ANIMATION_FRAMES] = {
    254, 252, 247, 237, 223, 201, 171, 133,
     86,  31, -30, -93, -154, -206, -242, -255,
};

extern const uint8_t clock_digits_start[] asm("_binary_clock_digits_bin_start");

static bool in_rounded_rect(int x, int y, int left, int top, int width, int height, int radius)
{
    if (x < left || y < top || x >= left + width || y >= top + height) {
        return false;
    }
    const int rx = x < left + radius ? left + radius :
                   (x >= left + width - radius ? left + width - radius - 1 : x);
    const int ry = y < top + radius ? top + radius :
                   (y >= top + height - radius ? top + height - radius - 1 : y);
    const int dx = x - rx;
    const int dy = y - ry;
    return dx * dx + dy * dy <= radius * radius;
}

static uint8_t digit_alpha(int digit, int origin_x, int x, int y)
{
    if (digit < 0 || digit > 9 || x < origin_x || y < DIGIT_Y) {
        return 0;
    }
    const int glyph_x = x - origin_x;
    const int glyph_y = y - DIGIT_Y;
    if (glyph_x < 0 || glyph_x >= DIGIT_W || glyph_y < 0 || glyph_y >= DIGIT_H) {
        return 0;
    }
    const size_t offset = (size_t)digit * DIGIT_H * DIGIT_ROW_BYTES +
                          (size_t)glyph_y * DIGIT_ROW_BYTES + glyph_x / 2;
    const uint8_t packed = clock_digits_start[offset];
    return (glyph_x & 1) ? (packed & 0x0F) : (packed >> 4);
}

static int digit_origin_x(const int digits[6], int card_index, int position)
{
    const int base = card_index * 2;
    const int first = digits[base];
    const int second = digits[base + 1];
    if (digits[base + position] < 0) return 0;

    if (first < 0) {
        const int width = s_digit_right[second] - s_digit_left[second] + 1;
        return s_card_x[card_index] + (CARD_W - width) / 2 - s_digit_left[second];
    }

    const int first_width = s_digit_right[first] - s_digit_left[first] + 1;
    const int second_width = s_digit_right[second] - s_digit_left[second] + 1;
    const int group_width = first_width + DIGIT_GAP + second_width;
    const int group_x = s_card_x[card_index] + (CARD_W - group_width) / 2;
    if (position == 0) return group_x - s_digit_left[first];
    return group_x + first_width + DIGIT_GAP - s_digit_left[second];
}

static void prepare_scene(clock_scene_t *scene)
{
    static const char *days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    if (scene->time_synced) {
        snprintf(scene->date, sizeof(scene->date), "%s %04d-%02d-%02d",
                 days[scene->timeinfo.tm_wday], scene->timeinfo.tm_year + 1900,
                 scene->timeinfo.tm_mon + 1, scene->timeinfo.tm_mday);
        strlcpy(scene->status, "NTP SYNC", sizeof(scene->status));
    } else {
        strlcpy(scene->date, "WAITING FOR TIME", sizeof(scene->date));
        strlcpy(scene->status, scene->wifi_connected ? "SYNCING" :
                (CONFIG_VOCAT_WIFI_SSID[0] ? "CONNECTING" : "SET WIFI"),
                sizeof(scene->status));
    }

    for (int card = 0; card < 3; ++card) {
        for (int position = 0; position < 2; ++position) {
            const int index = card * 2 + position;
            scene->digit_x[index] = digit_origin_x(scene->digits, card, position);
            scene->old_digit_x[index] = digit_origin_x(scene->old_digits, card, position);
        }
    }
}

static const uint8_t *glyph(char ch)
{
    static const uint8_t blank[7] = {0};
    static const uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
    };
    static const uint8_t A[7]={14,17,17,31,17,17,17}, C[7]={14,17,16,16,16,17,14};
    static const uint8_t D[7]={30,17,17,17,17,17,30}, E[7]={31,16,16,30,16,16,31};
    static const uint8_t F[7]={31,16,16,30,16,16,16}, G[7]={14,17,16,23,17,17,15};
    static const uint8_t H[7]={17,17,17,31,17,17,17}, I[7]={14,4,4,4,4,4,14};
    static const uint8_t K[7]={17,18,20,24,20,18,17}, L[7]={16,16,16,16,16,16,31};
    static const uint8_t M[7]={17,27,21,21,17,17,17}, N[7]={17,25,21,19,17,17,17};
    static const uint8_t O[7]={14,17,17,17,17,17,14}, P[7]={30,17,17,30,16,16,16};
    static const uint8_t R[7]={30,17,17,30,20,18,17}, S[7]={15,16,16,14,1,1,30};
    static const uint8_t T[7]={31,4,4,4,4,4,4}, U[7]={17,17,17,17,17,17,14};
    static const uint8_t V[7]={17,17,17,17,17,10,4}, W[7]={17,17,17,21,21,21,10};
    static const uint8_t Y[7]={17,17,10,4,4,4,4}, dash[7]={0,0,0,31,0,0,0};
    if (ch >= '0' && ch <= '9') return digits[ch - '0'];
    switch (ch) {
    case 'A': return A; case 'C': return C; case 'D': return D; case 'E': return E;
    case 'F': return F; case 'G': return G; case 'H': return H; case 'I': return I;
    case 'K': return K; case 'L': return L; case 'M': return M; case 'N': return N;
    case 'O': return O; case 'P': return P; case 'R': return R; case 'S': return S;
    case 'T': return T; case 'U': return U; case 'V': return V; case 'W': return W;
    case 'Y': return Y; case '-': return dash; default: return blank;
    }
}

static bool text_pixel(const char *text, int scale, int origin_x, int origin_y, int x, int y)
{
    if (y < origin_y || y >= origin_y + 7 * scale || x < origin_x) {
        return false;
    }
    const int advance = 6 * scale;
    const int index = (x - origin_x) / advance;
    if (index < 0 || index >= (int)strlen(text)) {
        return false;
    }
    const int local_x = (x - origin_x) % advance;
    if (local_x >= 5 * scale) {
        return false;
    }
    const int row = (y - origin_y) / scale;
    const int col = local_x / scale;
    return (glyph(text[index])[row] & (1U << (4 - col))) != 0;
}

static int centered_text_x(const char *text, int scale)
{
    const int width = (int)strlen(text) * 6 * scale - scale;
    return (SCREEN_W - width) / 2;
}

static void time_to_digits(const struct tm *timeinfo, int digits[6])
{
    int hour = timeinfo->tm_hour;
#if !CONFIG_VOCAT_CLOCK_24_HOUR
    hour %= 12;
    if (hour == 0) hour = 12;
    digits[0] = hour >= 10 ? hour / 10 : -1;
#else
    digits[0] = hour / 10;
#endif
    digits[1] = hour % 10;
    digits[2] = timeinfo->tm_min / 10;
    digits[3] = timeinfo->tm_min % 10;
    digits[4] = timeinfo->tm_sec / 10;
    digits[5] = timeinfo->tm_sec % 10;
}

static void prepare_animation_frame(clock_scene_t *scene, int step)
{
    const int projection = s_flip_projection[step];
    const bool folding_top = projection > 0;
    const int scale = folding_top ? projection : -projection;
    const int moving_edge = folding_top ?
                            FLIP_Y - ((FLIP_Y - DIGIT_Y) * scale) / 255 :
                            FLIP_Y + (((DIGIT_Y + DIGIT_H) - FLIP_Y) * scale) / 255;

    scene->animation_step = step;
    for (int local_y = 0; local_y < CARD_H; ++local_y) {
        const int y = CARD_Y + local_y;
        int sample_y = y;
        bool uses_old = false;
        bool on_flap = false;

        if (folding_top) {
            if (y < FLIP_Y) {
                if (y >= moving_edge) {
                    uses_old = true;
                    sample_y = FLIP_Y - ((FLIP_Y - y) * 255) / scale;
                    on_flap = true;
                }
            } else {
                uses_old = true;
            }
        } else if (y >= FLIP_Y) {
            if (y <= moving_edge) {
                sample_y = FLIP_Y + ((y - FLIP_Y) * 255) / scale;
                on_flap = true;
            } else {
                uses_old = true;
            }
        }

        scene->row_sample_y[local_y] = sample_y;
        scene->row_uses_old[local_y] = uses_old;
        scene->row_light[local_y] = on_flap ? 92 + (scale * 163) / 255 : 255;
        scene->row_is_edge[local_y] = scale < 247 && abs(y - moving_edge) <= 1;
    }
}

static uint16_t scene_pixel(const clock_scene_t *scene, int x, int y)
{
    const uint16_t background = vocat_bsp_rgb565(0, 0, 0);
    const uint16_t digit_color = vocat_bsp_rgb565(207, 207, 210);
    const uint16_t muted = vocat_bsp_rgb565(139, 139, 144);
    const uint16_t accent = vocat_bsp_rgb565(180, 180, 184);
    const uint16_t shadow = vocat_bsp_rgb565(3, 3, 4);

    if (y >= CARD_Y && y < CARD_Y + CARD_H) {
      for (int card_index = 0; card_index < 3; ++card_index) {
        const int cx = s_card_x[card_index];
        if (!in_rounded_rect(x, y, cx, CARD_Y, CARD_W, CARD_H, 12)) continue;
        if (!in_rounded_rect(x, y, cx + 1, CARD_Y + 1, CARD_W - 2, CARD_H - 2, 11)) {
            return vocat_bsp_rgb565(45, 45, 48);
        }
        const int base_digit = card_index * 2;
        const bool animated = scene->animation_step >= 0 &&
                              (scene->animated_cards & (1U << card_index));
        if (y >= FLIP_Y - 1 && y <= FLIP_Y + 1) return shadow;

        const int *visible_digits = scene->digits;
        const int *visible_x = scene->digit_x;
        int sample_y = y;
        int face_light = 255;

        if (animated) {
            const int local_y = y - CARD_Y;
            if (scene->row_is_edge[local_y]) return shadow;
            if (scene->row_uses_old[local_y]) {
                visible_digits = scene->old_digits;
                visible_x = scene->old_digit_x;
            }
            sample_y = scene->row_sample_y[local_y];
            face_light = scene->row_light[local_y];
        }

        for (int position = 0; position < 2; ++position) {
            const int digit_index = base_digit + position;
            const int digit = visible_digits[digit_index];
            const uint8_t alpha = digit_alpha(digit, visible_x[digit_index], x, sample_y);
            if (alpha != 0) {
                const int card_shade = 32 - ((y - CARD_Y) * 8) / CARD_H;
                int value = (card_shade * (15 - alpha) + 207 * alpha + 7) / 15;
                value = (value * face_light) / 255;
                return vocat_bsp_rgb565(value, value, value + 1);
            }
        }

        int shade = 32 - ((y - CARD_Y) * 8) / CARD_H;
        shade = (shade * face_light) / 255;
        return vocat_bsp_rgb565(shade, shade, shade + 1);
      }
      return background;
    }

    if (text_pixel("VOCAT", 2, centered_text_x("VOCAT", 2), 39, x, y)) return muted;
    if (text_pixel(scene->date, 2, centered_text_x(scene->date, 2), 278, x, y)) return digit_color;
    if (text_pixel(scene->status, 1, centered_text_x(scene->status, 1), 315, x, y)) return accent;
    return background;
}

static esp_err_t render_scene_area(const clock_scene_t *scene,
                                   int x_start, int y_start, int x_end, int y_end)
{
    const int width = x_end - x_start;
    for (int y0 = y_start; y0 < y_end; y0 += STRIPE_H) {
        const int rows = (y0 + STRIPE_H <= y_end) ? STRIPE_H : y_end - y0;
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < width; ++x) {
                s_stripe[y * width + x] = scene_pixel(scene, x_start + x, y0 + y);
            }
        }
        ESP_RETURN_ON_ERROR(vocat_bsp_display_draw_bitmap(x_start, y0, x_end, y0 + rows, s_stripe),
                            "clock_ui", "draw clock stripe");
    }
    return ESP_OK;
}

static esp_err_t render_scene(const clock_scene_t *scene)
{
    return render_scene_area(scene, 0, 0, SCREEN_W, SCREEN_H);
}

esp_err_t clock_ui_init(void)
{
    if (s_stripe == NULL) {
        s_stripe = heap_caps_malloc(SCREEN_W * STRIPE_H * sizeof(uint16_t),
                                    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    if (s_stripe == NULL) return ESP_ERR_NO_MEM;

    for (int digit = 0; digit < 10; ++digit) {
        int left = DIGIT_W;
        int right = -1;
        for (int y = 0; y < DIGIT_H; ++y) {
            for (int x = 0; x < DIGIT_W; ++x) {
                const size_t offset = (size_t)digit * DIGIT_H * DIGIT_ROW_BYTES +
                                      (size_t)y * DIGIT_ROW_BYTES + x / 2;
                const uint8_t packed = clock_digits_start[offset];
                const uint8_t alpha = (x & 1) ? (packed & 0x0F) : (packed >> 4);
                if (alpha != 0) {
                    if (x < left) left = x;
                    if (x > right) right = x;
                }
            }
        }
        s_digit_left[digit] = left < DIGIT_W ? left : 0;
        s_digit_right[digit] = right >= 0 ? right : DIGIT_W - 1;
    }
    return ESP_OK;
}

esp_err_t clock_ui_render(const struct tm *timeinfo, bool time_synced, bool wifi_connected)
{
    clock_scene_t scene = {
        .animation_step = -1,
        .time_synced = time_synced,
        .wifi_connected = wifi_connected,
    };
    if (timeinfo) scene.timeinfo = *timeinfo;
    if (time_synced && timeinfo) time_to_digits(timeinfo, scene.digits);
    else for (int i = 0; i < 6; ++i) scene.digits[i] = -1;
    memcpy(scene.old_digits, scene.digits, sizeof(scene.digits));
    prepare_scene(&scene);
    return render_scene(&scene);
}

esp_err_t clock_ui_animate(const struct tm *old_time, const struct tm *new_time,
                           bool time_synced, bool wifi_connected)
{
    if (!old_time || !new_time || !time_synced) return clock_ui_render(new_time, time_synced, wifi_connected);
    clock_scene_t scene = {
        .timeinfo = *new_time,
        .time_synced = true,
        .wifi_connected = wifi_connected,
    };
    time_to_digits(old_time, scene.old_digits);
    time_to_digits(new_time, scene.digits);
    prepare_scene(&scene);

    int first_changed_card = 3;
    int last_changed_card = -1;
    for (int card = 0; card < 3; ++card) {
        const int base = card * 2;
        if (scene.old_digits[base] != scene.digits[base] ||
            scene.old_digits[base + 1] != scene.digits[base + 1]) {
            scene.animated_cards |= 1U << card;
            if (card < first_changed_card) first_changed_card = card;
            last_changed_card = card;
        }
    }
    if (last_changed_card < 0) return ESP_OK;
    const int x_start = s_card_x[first_changed_card];
    const int x_end = s_card_x[last_changed_card] + CARD_W;

    TickType_t frame_deadline = xTaskGetTickCount();
    const TickType_t frame_period = pdMS_TO_TICKS(ANIMATION_FRAME_MS);
    for (int step = 0; step < ANIMATION_FRAMES; ++step) {
        prepare_animation_frame(&scene, step);
        ESP_RETURN_ON_ERROR(render_scene_area(&scene, x_start, CARD_Y, x_end, CARD_Y + CARD_H),
                            "clock_ui", "render flip frame");
        xTaskDelayUntil(&frame_deadline, frame_period);
    }
    scene.animation_step = -1;
    if (old_time->tm_yday != new_time->tm_yday || old_time->tm_year != new_time->tm_year) {
        return render_scene(&scene);
    }
    return render_scene_area(&scene, x_start, CARD_Y, x_end, CARD_Y + CARD_H);
}
