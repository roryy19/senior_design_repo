/*
 * sensor_task.c — Distance sensor reading task
 *
 * This is the "glue" that connects the VL53L1X driver to the distance
 * categorizer. It runs as a FreeRTOS task alongside the existing BLE
 * and beacon scanner tasks.
 *
 * What it does every 50ms:
 *   1. Check if sensor has new data    (vl53l1x_data_ready)
 *   2. Read distance in mm             (vl53l1x_read_distance)
 *   3. Check measurement validity      (vl53l1x_read_range_status)
 *   4. Convert mm → cm                 (divide by 10)
 *   5. Convert cm → motor level 0-7    (categorize_distance_cm)
 *   6. Print both to serial            (ESP_LOGI)
 *   7. Clear the interrupt             (vl53l1x_clear_interrupt)
 *
 * This follows the same FreeRTOS task pattern as beacon_scanner.c:
 *   - Static task function with infinite loop
 *   - Public _init() and _start() functions called from app_main
 *   - vTaskDelay for timing control
 */

#include "sensor_task.h"
#include "vl53l1x.h"
#include "distance_categorizer_wrapper.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "SENSOR";

/* Track whether init succeeded. If not, start() is a safe no-op. */
static bool s_sensor_ready = false;

/* ── The FreeRTOS task function ───────────────────────────────────── */
static void sensor_task(void *param)
{
    ESP_LOGI(TAG, "Sensor task started — reading every 50ms");

    while (1) {
        /* Step 1: Check if the sensor has a new measurement ready */
        bool ready = false;
        vl53l1x_data_ready(&ready);

        if (ready) {
            /* Step 2: Read the raw distance (in millimeters) */
            uint16_t distance_mm = 0;
            vl53l1x_read_distance(&distance_mm);

            /* Step 3: Read the range status to check validity */
            uint8_t range_status = 0;
            vl53l1x_read_range_status(&range_status);

            /* Step 4: Clear the interrupt so the sensor starts the next measurement */
            vl53l1x_clear_interrupt();

            /* Step 5: Process the reading */
            if (range_status == 0) {
                /* Valid measurement — convert and categorize */
                float distance_cm = (float)distance_mm / 10.0f;
                uint8_t motor_level = categorize_distance_cm(distance_cm);

                ESP_LOGI(TAG, "Distance: %6.1f cm | Motor level: %u/7",
                         distance_cm, motor_level);
            } else {
                /* Invalid measurement — log the status code.
                 * This matches the teammate's Arduino code where
                 * status != 0 means the reading isn't reliable.
                 *
                 * Status meanings:
                 *   1 = Sigma fail (too noisy)
                 *   2 = Signal fail (return signal too weak)
                 *   4 = Phase fail (out of bounds)
                 *   7 = Wrapped target
                 */
                ESP_LOGW(TAG, "Invalid reading (status=%u)", range_status);
            }
        }

        /* Wait 50ms before checking again.
         * The sensor produces a new reading every ~55ms (our inter-
         * measurement period), so checking every 50ms ensures we
         * catch each one promptly without busy-waiting. */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

bool sensor_task_init(void)
{
    ESP_LOGI(TAG, "Initializing distance sensor...");

    vl53l1x_err_t err = vl53l1x_init();
    if (err != VL53L1X_OK) {
        ESP_LOGE(TAG, "Sensor init failed (error %d). "
                 "Sensor task will not start.", err);
        s_sensor_ready = false;
        return false;
    }

    /* Start continuous ranging — the sensor begins taking measurements
     * immediately, but we won't read them until the task starts. */
    err = vl53l1x_start_ranging();
    if (err != VL53L1X_OK) {
        ESP_LOGE(TAG, "Failed to start ranging (error %d)", err);
        s_sensor_ready = false;
        return false;
    }

    s_sensor_ready = true;
    ESP_LOGI(TAG, "Sensor ready — ranging started");
    return true;
}

void sensor_task_start(void)
{
    if (!s_sensor_ready) {
        ESP_LOGW(TAG, "Sensor not initialized — skipping task creation");
        return;
    }

    /* Create the FreeRTOS task.
     * - Stack: 4096 bytes (plenty for our simple read-categorize-print loop)
     * - Priority: 5 (higher than beacon scanner at 3, matching the planned
     *   architecture where sensor reading is the highest-priority task) */
    xTaskCreate(sensor_task, "sensor", 4096, NULL, 5, NULL);
}
