/*
 * sensor_task.c — Multi-sensor reading task with motor output
 *
 * Reads multiple VL53L1X distance sensors via two PCA9548A I2C
 * multiplexers, runs the full processing pipeline (distance →
 * motor mapping → bit packing), and outputs to 74HC595 shift
 * registers to drive vibration motors.
 *
 * What it does every 50ms:
 *   1. For each sensor: select mux channel, read distance
 *   2. Run pipeline: 12 distances → 8 motor levels → 3 packed bytes
 *   3. Shift out packed bytes to motor shift registers
 *   4. Log changes to serial monitor
 *
 * This follows the same FreeRTOS task pattern as beacon_scanner.c:
 *   - Static task function with infinite loop
 *   - Public _init() and _start() functions called from app_main
 *   - vTaskDelay for timing control
 *
 * Multi-sensor architecture:
 *   - Two PCA9548A muxes on the same I2C bus (GPIO 8 SDA, GPIO 9 SCL)
 *   - MUX0 (0x70): VL53L0X-type sensors
 *   - MUX1 (0x71): VL53L1X-type sensors
 *   - All sensors share address 0x29; mux selects which is on the bus
 *   - Each sensor runs continuous ranging independently
 */

#include "sensor_task.h"
#include "vl53l1x.h"
#include "pca9548a.h"
#include "pipeline_wrapper.h"
#include "shift_register.h"

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "SENSOR";

/* ── I2C bus pins (shared with muxes and all sensors) ────────────── */
#define I2C_SDA_PIN     8
#define I2C_SCL_PIN     9
#define I2C_FREQ_HZ     400000

/* ── Sensor configuration ────────────────────────────────────────── */

/* Each sensor is identified by which mux it's on and which channel.
 * sensor_index maps to the pipeline's 12-sensor array:
 *   indices 0-9  = belt sensors (10 sensors at 36-degree intervals)
 *   indices 10-11 = front sensors (up-angled, down-angled)
 *
 * For now we only have 2 test sensors. The rest of the 12-entry
 * distance array stays at 999.0f (no obstacle → motor off). */

typedef struct {
    uint8_t mux_addr;       /* PCA9548A address: 0x70 or 0x71 */
    uint8_t mux_channel;    /* Channel on that mux: 0-7 */
    uint8_t sensor_index;   /* Index into the 12-sensor pipeline array */
    bool    ok;             /* Did this sensor initialize successfully? */
} sensor_entry_t;

/* ── Active sensors (expand this table as you wire more sensors) ─── */
#define NUM_ACTIVE_SENSORS  2

static sensor_entry_t s_sensors[NUM_ACTIVE_SENSORS] = {
    /* Test sensor 1: MUX1 (0x71) channel 0
     * Mapped to pipeline index 0 (belt sensor 0, 0-degree / front) */
    { .mux_addr = PCA9548A_MUX1_ADDR, .mux_channel = 0,
      .sensor_index = 0, .ok = false },

    /* Test sensor 2: MUX0 (0x70) channel 1
     * Mapped to pipeline index 1 (belt sensor 1, 36-degree) */
    { .mux_addr = PCA9548A_MUX0_ADDR, .mux_channel = 1,
      .sensor_index = 1, .ok = false },
};

/* Track whether init succeeded */
static bool s_sensor_ready = false;

/* Persistent distance array for all 12 pipeline slots */
static float s_distances_cm[PIPELINE_TOTAL_SENSORS];

/* Previous motor levels — only log when they change */
static uint8_t s_prev_motor_levels[PIPELINE_NUM_MOTORS];
static bool s_first_reading = true;

/* ── The FreeRTOS task function ───────────────────────────────────── */
static void sensor_task(void *param)
{
    ESP_LOGI(TAG, "Sensor task started — reading %d sensors every 50ms",
             NUM_ACTIVE_SENSORS);

    while (1) {
        /* ── Step 1: Read each sensor via its mux channel ──────────── */
        for (int i = 0; i < NUM_ACTIVE_SENSORS; i++) {
            if (!s_sensors[i].ok) continue;

            /* Select this sensor's mux channel (disables the other mux) */
            pca9548a_select(s_sensors[i].mux_addr, s_sensors[i].mux_channel);

            /* Check if this sensor has a new measurement ready */
            bool ready = false;
            vl53l1x_data_ready(&ready);

            if (ready) {
                uint16_t distance_mm = 0;
                vl53l1x_read_distance(&distance_mm);

                uint8_t range_status = 0;
                vl53l1x_read_range_status(&range_status);
                vl53l1x_clear_interrupt();

                if (range_status == 0) {
                    /* Valid reading — store in pipeline array */
                    s_distances_cm[s_sensors[i].sensor_index] =
                        (float)distance_mm / 10.0f;
                } else {
                    /* Invalid reading — treat as "no obstacle" */
                    s_distances_cm[s_sensors[i].sensor_index] = 999.0f;
                }
            }
        }

        /* ── Step 2: Run the full pipeline ─────────────────────────── */
        uint8_t shift_data[PIPELINE_SHIFT_REG_BYTES];
        uint8_t motor_levels[PIPELINE_NUM_MOTORS];
        pipeline_process(s_distances_cm, shift_data, motor_levels);

        /* ── Step 3: Output to shift registers (drive motors) ──────── */
        shift_register_send(shift_data, PIPELINE_SHIFT_REG_BYTES);

        /* ── Step 4: Log changes ───────────────────────────────────── */
        if (s_first_reading ||
            memcmp(motor_levels, s_prev_motor_levels, PIPELINE_NUM_MOTORS) != 0) {

            /* Log each active sensor's distance */
            for (int i = 0; i < NUM_ACTIVE_SENSORS; i++) {
                if (!s_sensors[i].ok) continue;
                int idx = s_sensors[i].sensor_index;
                ESP_LOGI(TAG, "  Sensor %d (mux 0x%02X ch%d): %6.1f cm",
                         idx, s_sensors[i].mux_addr, s_sensors[i].mux_channel,
                         s_distances_cm[idx]);
            }

            /* Log motor levels that changed */
            for (int m = 0; m < PIPELINE_NUM_MOTORS; m++) {
                if (s_first_reading || motor_levels[m] != s_prev_motor_levels[m]) {
                    ESP_LOGI(TAG, "  Motor %d: level %u/7 (was %u)",
                             m, motor_levels[m],
                             s_first_reading ? 0 : s_prev_motor_levels[m]);
                }
            }

            memcpy(s_prev_motor_levels, motor_levels, PIPELINE_NUM_MOTORS);
            s_first_reading = false;
        }

        /* ── Step 5: Wait before next cycle ────────────────────────── */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

bool sensor_task_init(void)
{
    ESP_LOGI(TAG, "Initializing sensors (%d active)...", NUM_ACTIVE_SENSORS);

    /* Initialize all distances to "far away" (no obstacle detected) */
    for (int i = 0; i < PIPELINE_TOTAL_SENSORS; i++) {
        s_distances_cm[i] = 999.0f;
    }
    memset(s_prev_motor_levels, 0, sizeof(s_prev_motor_levels));

    /* ── Step 1: Create the I2C bus ──────────────────────────────── */
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t rc = i2c_new_master_bus(&bus_cfg, &bus);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(rc));
        return false;
    }
    ESP_LOGI(TAG, "I2C bus created (SDA=%d, SCL=%d, %dkHz)",
             I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ / 1000);

    /* ── Step 2: Initialize both PCA9548A multiplexers ───────────── */
    rc = pca9548a_init(bus);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Mux init failed — cannot reach sensors");
        return false;
    }

    /* ── Step 3: Add VL53L1X device to the bus (once) ────────────── */
    vl53l1x_err_t verr = vl53l1x_create_on_bus(bus);
    if (verr != VL53L1X_OK) {
        ESP_LOGE(TAG, "Failed to add VL53L1X device to bus");
        return false;
    }

    /* ── Step 4: Initialize each sensor on its mux channel ───────── */
    int ok_count = 0;
    for (int i = 0; i < NUM_ACTIVE_SENSORS; i++) {
        /* Select this sensor's mux channel */
        pca9548a_select(s_sensors[i].mux_addr, s_sensors[i].mux_channel);
        vTaskDelay(pdMS_TO_TICKS(10));  /* brief settle time after mux switch */

        /* Run the full sensor init sequence (boot, config, calibration) */
        verr = vl53l1x_sensor_init();
        if (verr != VL53L1X_OK) {
            ESP_LOGW(TAG, "Sensor %d (mux 0x%02X ch%d) init FAILED",
                     i, s_sensors[i].mux_addr, s_sensors[i].mux_channel);
            s_sensors[i].ok = false;
            continue;
        }

        /* Start continuous ranging on this sensor */
        verr = vl53l1x_start_ranging();
        if (verr != VL53L1X_OK) {
            ESP_LOGW(TAG, "Sensor %d ranging start FAILED", i);
            s_sensors[i].ok = false;
            continue;
        }

        s_sensors[i].ok = true;
        ok_count++;
        ESP_LOGI(TAG, "Sensor %d (mux 0x%02X ch%d) → pipeline index %d — OK",
                 i, s_sensors[i].mux_addr, s_sensors[i].mux_channel,
                 s_sensors[i].sensor_index);
    }

    s_sensor_ready = (ok_count > 0);
    if (s_sensor_ready) {
        ESP_LOGI(TAG, "%d/%d sensors ready", ok_count, NUM_ACTIVE_SENSORS);
    } else {
        ESP_LOGE(TAG, "No sensors initialized — task will not start");
    }
    return s_sensor_ready;
}

void sensor_task_start(void)
{
    if (!s_sensor_ready) {
        ESP_LOGW(TAG, "Sensors not initialized — skipping task creation");
        return;
    }

    /* Create the FreeRTOS task.
     * - Stack: 4096 bytes (sensor reads + pipeline + logging)
     * - Priority: 5 (higher than beacon scanner at 3) */
    xTaskCreate(sensor_task, "sensor", 4096, NULL, 5, NULL);
}
