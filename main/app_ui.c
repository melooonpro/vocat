/* SPDX-License-Identifier: Apache-2.0 */

#include "app_ui.h"
#include "air_mouse.h"
#include "bsp/esp_vocat.h"
#include "clock_ui.h"
#include "eaf_ui.h"
#include "esp_check.h"
#include "esp_log.h"
#include "mouse_ui.h"
#include "voice_ui.h"
#include "voice_usb.h"
#include "vocat_v1_0.h"

typedef enum {
    APP_PAGE_EAF = 0,
    APP_PAGE_CLOCK,
    APP_PAGE_VOICE,
    APP_PAGE_MOUSE,
    APP_PAGE_COUNT,
} app_page_t;

static lv_obj_t *s_pages[APP_PAGE_COUNT];
static volatile app_page_t s_page = APP_PAGE_EAF;
static const char *TAG = "vocat_ui";
static const char *const s_page_names[] = {"EAF", "Clock", "Mic", "Mouse"};

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

    /* Page 0/1 keep the native USB Serial/JTAG download port available. */
    voice_usb_set_page_active(page == APP_PAGE_VOICE || page == APP_PAGE_MOUSE);
    eaf_ui_set_active(page == APP_PAGE_EAF);
    if (page == APP_PAGE_MOUSE) air_mouse_set_active(true);

    for (int i = 0; i < APP_PAGE_COUNT; ++i) {
        slide(s_pages[i], lv_obj_get_x(s_pages[i]), (i - (int)page) * 360);
    }
    s_page = page;
}

static void gesture_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_GESTURE_LEFT && s_page + 1 < APP_PAGE_COUNT) {
        show_page((app_page_t)(s_page + 1));
    } else if (code == LV_EVENT_GESTURE_RIGHT && s_page > APP_PAGE_EAF) {
        show_page((app_page_t)(s_page - 1));
    }
}

esp_err_t app_ui_init(void)
{
    ESP_RETURN_ON_ERROR(vocat_v1_0_prepare_display(), "app_ui", "prepare v1.0 display");
    /* Keep DMA draw buffers small: the BSP default uses two 360x100
     * RGB565 buffers (144 KB) in internal RAM, even with PSRAM enabled.
     * 36 rows use 51,840 bytes total and retain asynchronous double buffering.
     * Respect a smaller configured SPI transfer height, if selected. */
    const uint32_t draw_rows = CONFIG_BSP_LCD_DRAW_BUF_HEIGHT < 36
                                   ? CONFIG_BSP_LCD_DRAW_BUF_HEIGHT : 36;
    const bsp_display_cfg_t display_config = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * draw_rows,
        .double_buffer = true,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false,
        },
    };
    ESP_LOGI(TAG, "Display DMA buffers: 2 x %lu rows", (unsigned long)draw_rows);
    if (bsp_display_start_with_config(&display_config) == NULL) return ESP_FAIL;
    if (!bsp_display_lock(0)) return ESP_ERR_TIMEOUT;
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(screen, false);
    s_pages[APP_PAGE_EAF] = eaf_ui_create(screen);
    s_pages[APP_PAGE_CLOCK] = clock_ui_create(screen);
    s_pages[APP_PAGE_VOICE] = voice_ui_create(screen);
    s_pages[APP_PAGE_MOUSE] = mouse_ui_create(screen);
    for (int i = 0; i < APP_PAGE_COUNT; ++i) {
        lv_obj_set_pos(s_pages[i], i * 360, 0);
    }
    voice_usb_set_page_active(false);
    lv_obj_add_event_cb(screen, gesture_event, LV_EVENT_GESTURE_LEFT, NULL);
    lv_obj_add_event_cb(screen, gesture_event, LV_EVENT_GESTURE_RIGHT, NULL);
    bsp_display_unlock();
    esp_err_t touch_result = vocat_v1_0_top_touch_init();
    if (touch_result != ESP_OK) {
        ESP_LOGE(TAG, "Unable to initialize top touch pad: %s",
                 esp_err_to_name(touch_result));
    }
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
