/* SPDX-License-Identifier: Apache-2.0 */

#include "air_mouse.h"

#include <math.h>
#include <stdatomic.h>
#include "bmi270.h"
#include "bsp/esp_vocat.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "voice_usb.h"

#define AIR_MOUSE_SAMPLE_MS 10
#define AIR_MOUSE_CALIBRATION_SAMPLES 60
#define AIR_MOUSE_DEAD_ZONE_DPS 1.8f
#define AIR_MOUSE_CALIBRATION_STILL_DPS 8.0f
#define AIR_MOUSE_STATIONARY_DPS 3.5f
#define AIR_MOUSE_STATIONARY_SAMPLES 30
#define AIR_MOUSE_BIAS_TRACK_ALPHA 0.02f
#define AIR_MOUSE_STATUS_LOG_MS 10000

static const char *TAG = "air_mouse";
static atomic_bool s_active;
static atomic_bool s_scroll_active;
static atomic_bool s_ready;
static atomic_bool s_calibrating;
static atomic_int s_last_dx;
static atomic_int s_last_dy;
static bmi270_handle_t *s_imu;

static esp_err_t init_sensor(void)
{
    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) return err;

    const bmi270_driver_config_t driver_config = {
        .addr = BMI270_I2C_ADDRESS_L,
        .interface = BMI270_USE_I2C,
        .i2c_bus = bsp_i2c_get_handle(),
    };
    err = bmi270_create(&driver_config, &s_imu);
    if (err != ESP_OK) return err;

    const bmi270_config_t measurement_config = {
        .acce_odr = BMI270_ACC_ODR_100_HZ,
        .acce_range = BMI270_ACC_RANGE_4_G,
        .gyro_odr = BMI270_GYR_ODR_100_HZ,
        .gyro_range = BMI270_GYR_RANGE_500_DPS,
    };
    err = bmi270_start(s_imu, &measurement_config);
    if (err != ESP_OK) {
        bmi270_delete(s_imu);
        s_imu = NULL;
        return err;
    }

    uint8_t chip_id = 0;
    bmi270_get_chip_id(s_imu, &chip_id);
    ESP_LOGI(TAG, "BMI270 ready (chip ID 0x%02x)", chip_id);
    atomic_store(&s_ready, true);
    return ESP_OK;
}

static float shaped_delta(float rate_dps)
{
    float magnitude = fabsf(rate_dps);
    if (magnitude <= AIR_MOUSE_DEAD_ZONE_DPS) return 0.0f;
    magnitude -= AIR_MOUSE_DEAD_ZONE_DPS;
    float pixels = magnitude * 0.055f + magnitude * magnitude * 0.00040f;
    return rate_dps < 0.0f ? -pixels : pixels;
}

static int take_whole_pixels(float *value)
{
    int whole = (int)*value;
    *value -= (float)whole;
    return whole;
}

static bool calibrate(float *bias_x, float *bias_y, float *bias_z)
{
    atomic_store(&s_calibrating, true);
    ESP_LOGI(TAG, "Calibration started; keep the board still");
    *bias_x = 0.0f;
    *bias_y = 0.0f;
    *bias_z = 0.0f;
    unsigned collected = 0;

    while (atomic_load(&s_active) && collected < AIR_MOUSE_CALIBRATION_SAMPLES) {
        float x, y, z;
        if (bmi270_get_gyro_data(s_imu, &x, &y, &z) == ESP_OK) {
            if (fabsf(x) <= AIR_MOUSE_CALIBRATION_STILL_DPS &&
                fabsf(y) <= AIR_MOUSE_CALIBRATION_STILL_DPS &&
                fabsf(z) <= AIR_MOUSE_CALIBRATION_STILL_DPS) {
                *bias_x += x;
                *bias_y += y;
                *bias_z += z;
                ++collected;
            } else {
                /* Require one continuous still window; samples collected
                 * while the user is moving would become permanent drift. */
                *bias_x = *bias_y = *bias_z = 0.0f;
                collected = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(AIR_MOUSE_SAMPLE_MS));
    }

    if (collected != AIR_MOUSE_CALIBRATION_SAMPLES) {
        atomic_store(&s_calibrating, false);
        ESP_LOGW(TAG, "Calibration cancelled after %u/%u still samples",
                 collected, AIR_MOUSE_CALIBRATION_SAMPLES);
        return false;
    }
    *bias_x /= collected;
    *bias_y /= collected;
    *bias_z /= collected;
    atomic_store(&s_calibrating, false);
    ESP_LOGI(TAG, "Air mouse calibrated: %.2f, %.2f, %.2f dps",
             *bias_x, *bias_y, *bias_z);
    return true;
}

static void air_mouse_task(void *ctx)
{
    (void)ctx;
    while (init_sensor() != ESP_OK) {
        ESP_LOGW(TAG, "BMI270 unavailable; retrying");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    bool was_active = false;
    float bias_x = 0.0f, bias_y = 0.0f, bias_z = 0.0f;
    float filtered_x = 0.0f, filtered_y = 0.0f;
    float remainder_x = 0.0f, remainder_y = 0.0f;
    unsigned stationary_samples = 0;
    TickType_t last_status_log = xTaskGetTickCount();
    unsigned sample_count = 0;
    unsigned movement_count = 0;
    unsigned read_errors = 0;

    while (true) {
        bool active = atomic_load(&s_active);
        if (!active) {
            was_active = false;
            atomic_store(&s_calibrating, false);
            atomic_store(&s_last_dx, 0);
            atomic_store(&s_last_dy, 0);
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        if (!was_active) {
            voice_usb_mouse_release_all();
            filtered_x = filtered_y = 0.0f;
            remainder_x = remainder_y = 0.0f;
            stationary_samples = 0;
            if (!calibrate(&bias_x, &bias_y, &bias_z)) continue;
            was_active = true;
            last_status_log = xTaskGetTickCount();
            sample_count = movement_count = read_errors = 0;
        }

        float gyro_x, gyro_y, gyro_z;
        if (bmi270_get_gyro_data(s_imu, &gyro_x, &gyro_y, &gyro_z) != ESP_OK) {
            ++read_errors;
            vTaskDelay(pdMS_TO_TICKS(AIR_MOUSE_SAMPLE_MS));
            continue;
        }
        ++sample_count;
        /* Cursor axes are intentionally mapped independently of the BMI270
         * axis names: screen X uses gyro Y, while screen Y uses gyro X. */
        gyro_x -= bias_x;
        gyro_y -= bias_y;
        gyro_z -= bias_z;

        bool stationary_candidate = fabsf(gyro_x) <= AIR_MOUSE_STATIONARY_DPS &&
                                    fabsf(gyro_y) <= AIR_MOUSE_STATIONARY_DPS &&
                                    fabsf(gyro_z) <= AIR_MOUSE_STATIONARY_DPS;
        if (stationary_candidate) {
            if (stationary_samples < AIR_MOUSE_STATIONARY_SAMPLES) {
                ++stationary_samples;
            }
        } else {
            stationary_samples = 0;
        }

        bool stationary = stationary_samples >= AIR_MOUSE_STATIONARY_SAMPLES;
        if (stationary) {
            /* Slowly track temperature-dependent gyro zero bias only after
             * 300 ms of confirmed stillness. Clearing the fractional pixel
             * accumulators prevents sub-pixel noise becoming eventual drift. */
            bias_x += gyro_x * AIR_MOUSE_BIAS_TRACK_ALPHA;
            bias_y += gyro_y * AIR_MOUSE_BIAS_TRACK_ALPHA;
            bias_z += gyro_z * AIR_MOUSE_BIAS_TRACK_ALPHA;
            filtered_x = filtered_y = 0.0f;
            remainder_x = remainder_y = 0.0f;
            atomic_store(&s_last_dx, 0);
            atomic_store(&s_last_dy, 0);
        } else {
            filtered_x = filtered_x * 0.70f + gyro_x * 0.30f;
            filtered_y = filtered_y * 0.70f + gyro_y * 0.30f;
        }

        if (!stationary && !atomic_load(&s_scroll_active)) {
            /* Match the on-screen cursor direction to the physical motion.
             * Vertical travel intentionally uses twice the horizontal gain. */
            remainder_x += shaped_delta(filtered_y) * 1.5f;
            remainder_y += shaped_delta(filtered_x) * 2.0f;
            int dx = take_whole_pixels(&remainder_x);
            int dy = take_whole_pixels(&remainder_y);
            atomic_store(&s_last_dx, dx);
            atomic_store(&s_last_dy, dy);
            if (dx != 0 || dy != 0) {
                voice_usb_mouse_move(dx, dy);
                ++movement_count;
            }
        } else {
            atomic_store(&s_last_dx, 0);
            atomic_store(&s_last_dy, 0);
        }
        TickType_t now = xTaskGetTickCount();
        if (now - last_status_log >= pdMS_TO_TICKS(AIR_MOUSE_STATUS_LOG_MS)) {
            ESP_LOGI(TAG, "Mouse status: IMU samples=%u, movement updates=%u, read errors=%u, scroll=%s, stationary=%s",
                     sample_count, movement_count, read_errors,
                     atomic_load(&s_scroll_active) ? "on" : "off",
                     stationary ? "yes" : "no");
            sample_count = movement_count = read_errors = 0;
            last_status_log = now;
        }
        vTaskDelay(pdMS_TO_TICKS(AIR_MOUSE_SAMPLE_MS));
    }
}

esp_err_t air_mouse_init(void)
{
    BaseType_t result = xTaskCreatePinnedToCore(air_mouse_task, "air_mouse", 4096,
                                                NULL, 5, NULL, tskNO_AFFINITY);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void air_mouse_set_active(bool active)
{
    bool previous = atomic_exchange(&s_active, active);
    if (previous != active) {
        ESP_LOGI(TAG, "Mouse page %s", active ? "active" : "inactive");
    }
    if (!active) {
        atomic_store(&s_scroll_active, false);
        voice_usb_mouse_release_all();
    }
}

void air_mouse_set_scroll_active(bool active) { atomic_store(&s_scroll_active, active); }
bool air_mouse_is_ready(void) { return atomic_load(&s_ready); }
bool air_mouse_is_calibrating(void) { return atomic_load(&s_calibrating); }
int8_t air_mouse_last_dx(void) { return (int8_t)atomic_load(&s_last_dx); }
int8_t air_mouse_last_dy(void) { return (int8_t)atomic_load(&s_last_dy); }
