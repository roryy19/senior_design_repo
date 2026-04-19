/*
 * battery_monitor.c — see battery_monitor.h for pin map and thresholds.
 *
 * The whole module compiles but only the start() function does real work.
 * With BATTERY_MONITOR_ENABLED = 0 in the header, main.c never calls it.
 */

#include "battery_monitor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "BATT";

/* Pins */
#define ADC_GPIO            GPIO_NUM_4
#define ADC_UNIT            ADC_UNIT_1
#define ADC_CHANNEL         ADC_CHANNEL_3   /* GPIO 4 on ESP32-S3 → ADC1_CH3 */
#define SHUTDOWN_GPIO_A     GPIO_NUM_11
#define SHUTDOWN_GPIO_B     GPIO_NUM_12

/* Thresholds (millivolts measured at the ADC pin, after divider) */
#define ALERT_MV            2550    /* phone popup fires below this      */
#define ALERT_CLEAR_MV      2600    /* hysteresis: re-arm alert above this */
#define SHUTDOWN_MV         2500    /* buck converters disabled below this */

/* Debounce: require N consecutive reads below threshold before acting */
#define DEBOUNCE_N          5

/* Sample period */
#define SAMPLE_PERIOD_MS    1000

static void battery_task(void *arg)
{
    /* ADC oneshot init */
    adc_oneshot_unit_handle_t adc = NULL;
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,   /* full 0–~3.1 V input range */
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_CHANNEL, &chan_cfg));

    /* Calibration (raw → mV) */
    adc_cali_handle_t cali = NULL;
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT,
        .chan     = ADC_CHANNEL,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t cali_ok = adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali);
    if (cali_ok != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration failed (%d); readings will be raw-mapped", cali_ok);
    }

    /* Shutdown GPIOs start LOW (bucks enabled) */
    gpio_config_t sd_cfg = {
        .pin_bit_mask = (1ULL << SHUTDOWN_GPIO_A) | (1ULL << SHUTDOWN_GPIO_B),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&sd_cfg));
    gpio_set_level(SHUTDOWN_GPIO_A, 0);
    gpio_set_level(SHUTDOWN_GPIO_B, 0);

    ESP_LOGI(TAG, "Battery monitor running (GPIO %d ADC, shutdown on GPIO %d + %d)",
             ADC_GPIO, SHUTDOWN_GPIO_A, SHUTDOWN_GPIO_B);

    int alert_count = 0;
    int shutdown_count = 0;
    bool alert_latched = false;   /* true while below ALERT_MV until hysteresis clears */

    for (;;) {
        int raw = 0;
        if (adc_oneshot_read(adc, ADC_CHANNEL, &raw) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
            continue;
        }

        int mv = 0;
        if (cali) {
            adc_cali_raw_to_voltage(cali, raw, &mv);
        } else {
            /* Fallback: assume 12-bit + ~3.1 V range */
            mv = (raw * 3100) / 4095;
        }

        /* --- Shutdown check (takes priority) --- */
        if (mv < SHUTDOWN_MV) {
            shutdown_count++;
            if (shutdown_count >= DEBOUNCE_N) {
                ESP_LOGE(TAG, "Battery critical (%d mV). Disabling both buck converters.", mv);
                gpio_set_level(SHUTDOWN_GPIO_A, 1);
                gpio_set_level(SHUTDOWN_GPIO_B, 1);
                vTaskDelete(NULL);   /* one-way: stop the task */
                return;
            }
        } else {
            shutdown_count = 0;
        }

        /* --- Alert check with hysteresis --- */
        if (!alert_latched) {
            if (mv < ALERT_MV) {
                alert_count++;
                if (alert_count >= DEBOUNCE_N) {
                    ESP_LOGW(TAG, "Battery low (%d mV). Notifying phone.", mv);
                    send_battery_alert();
                    alert_latched = true;
                    alert_count = 0;
                }
            } else {
                alert_count = 0;
            }
        } else {
            /* Wait for voltage to recover past ALERT_CLEAR_MV before re-arming */
            if (mv >= ALERT_CLEAR_MV) {
                alert_latched = false;
                ESP_LOGI(TAG, "Battery recovered (%d mV). Alert re-armed.", mv);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}

void battery_monitor_start(void)
{
    xTaskCreate(battery_task, "battery", 3072, NULL, 4, NULL);
}
