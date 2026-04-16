/*
 * sensor_task.c — Multi-sensor reading task with motor output
 *
 * Reads multiple distance sensors (VL53L1X and VL53L0X) via two
 * PCA9548A I2C multiplexers, runs the full processing pipeline
 * (distance → motor mapping → bit packing), and outputs to 74HC595
 * shift registers to drive vibration motors.
 *
 * What it does every 50ms:
 *   1. For each sensor: select mux channel, read distance
 *   2. Run pipeline: 12 distances → 8 motor levels → 3 packed bytes
 *   3. Shift out packed bytes to motor shift registers
 *   4. Log changes to serial monitor
 *
 * Single-sensor test mode: when only 1 sensor initializes successfully,
 * its distance reading is automatically copied to ALL pipeline slots
 * so that ALL motors respond. This makes testing easy with just one
 * sensor. When 2+ sensors are active, normal angular mapping is used.
 *
 * Sensor types:
 *   - VL53L1X: 16-bit register addressing, used for front arc sensors
 *   - VL53L0X: 8-bit register addressing, used for side/back sensors
 *   - Both share I2C address 0x29; mux selects which is active
 *
 * Multi-sensor architecture:
 *   - Two PCA9548A muxes on the same I2C bus (GPIO 8 SDA, GPIO 9 SCL)
 *   - MUX0 (0x70): 5 VL53L0X sensors (ch0-ch4) — sides/back
 *   - MUX1 (0x71): 7 VL53L1X sensors (ch0-ch6) — front arc
 *   - One shared I2C device handle at address 0x29 for both sensor types
 *   - Each sensor runs continuous ranging independently
 */

#include "sensor_task.h"
#include "vl53l1x.h"
#include "vl53l0x.h"
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
#define I2C_SDA_PIN      8
#define I2C_SCL_PIN      9
#define I2C_FREQ_HZ      400000
#define SENSOR_I2C_ADDR  0x29   /* Shared by both VL53L0X and VL53L1X */

/* Uncomment to broadcast a single sensor's reading to ALL motors.
 * When commented out, normal angular mapping is used even with 1 sensor. */
// #define SINGLE_SENSOR_BROADCAST

/* ── Sensor types ────────────────────────────────────────────────── */
#define SENSOR_TYPE_L0X  0      /* VL53L0X (8-bit registers, ~120cm range) */
#define SENSOR_TYPE_L1X  1      /* VL53L1X (16-bit registers, ~400cm range) */

/* ── Sensor configuration ────────────────────────────────────────── */

typedef struct {
    uint8_t mux_addr;       /* PCA9548A address: 0x70 or 0x71 */
    uint8_t mux_channel;    /* Channel on that mux: 0-7 */
    uint8_t sensor_type;    /* SENSOR_TYPE_L1X or SENSOR_TYPE_L0X */
    uint8_t sensor_index;   /* Index into the 12-sensor pipeline array */
    bool    ok;             /* Did this sensor initialize successfully? */
} sensor_entry_t;

/*
 * Sensor table — 12 physical sensors mapped to mux addresses/channels.
 *
 * MUX1 (0x71) has all 7 VL53L1X sensors (front arc + front up/down).
 * MUX0 (0x70) has all 5 VL53L0X sensors (sides and back).
 * Channel numbers are sequential within each mux (0,1,2,...).
 *
 * IMPORTANT: sensor_index values are FIXED by the pipeline code in
 * sensor_config.h. They cannot be renumbered:
 *
 *   sensor_config.h defines BELT_SENSOR_ANGLES[10] = {0, 36, 72, ...}
 *   So pipeline index 0 is ALWAYS 0°, index 1 is ALWAYS 36°, etc.
 *   Indices 10-11 are ALWAYS front up/down (both treated as 0°).
 *
 *   Pipeline index → belt angle → nearest motor(s):
 *     0  → 0°   (front center)  → motor 0
 *     1  → 36°  (front-right)   → motors 0,1
 *     2  → 72°  (right-front)   → motors 1,2
 *     3  → 108° (right-back)    → motors 2,3
 *     4  → 144° (back-right)    → motors 3,4
 *     5  → 180° (back center)   → motor 4
 *     6  → 216° (back-left)     → motors 4,5
 *     7  → 252° (left-back)     → motors 5,6
 *     8  → 288° (left-front)    → motors 6,7
 *     9  → 324° (front-left)    → motors 7,0
 *     10 → front up-angled      → motor 0
 *     11 → front down-angled    → motor 0
 */
#define NUM_ACTIVE_SENSORS  12

static sensor_entry_t s_sensors[NUM_ACTIVE_SENSORS] = {
    /* ── MUX1 (0x71) — 7 VL53L1X sensors (front arc) ──────────────── */
    /*    Three front sensors at 0°: belt-level, up-angled, down-angled.
     *    If up/down are swapped, swap sensor_index 10 and 11 below. */
    { .mux_addr = 0x71, .mux_channel = 0, .sensor_type = SENSOR_TYPE_L1X,
      .sensor_index = 0,  .ok = false },   /* 0°   — front center (belt) */
    { .mux_addr = 0x71, .mux_channel = 1, .sensor_type = SENSOR_TYPE_L1X,
      .sensor_index = 10, .ok = false },   /* 0°   — front up-angled */
    { .mux_addr = 0x71, .mux_channel = 2, .sensor_type = SENSOR_TYPE_L1X,
      .sensor_index = 11, .ok = false },   /* 0°   — front down-angled */
    { .mux_addr = 0x71, .mux_channel = 3, .sensor_type = SENSOR_TYPE_L1X,
      .sensor_index = 1,  .ok = false },   /* 36°  — front-right */
    { .mux_addr = 0x71, .mux_channel = 4, .sensor_type = SENSOR_TYPE_L1X,
      .sensor_index = 2,  .ok = false },   /* 72°  — right-front */
    { .mux_addr = 0x71, .mux_channel = 5, .sensor_type = SENSOR_TYPE_L1X,
      .sensor_index = 8,  .ok = false },   /* 288° — left-front */
    { .mux_addr = 0x71, .mux_channel = 6, .sensor_type = SENSOR_TYPE_L1X,
      .sensor_index = 9,  .ok = false },   /* 324° — front-left */

    /* ── MUX0 (0x70) — 5 VL53L0X sensors (sides / back) ──────────── */
    { .mux_addr = 0x70, .mux_channel = 0, .sensor_type = SENSOR_TYPE_L0X,
      .sensor_index = 3,  .ok = false },   /* 108° — right-back */
    { .mux_addr = 0x70, .mux_channel = 1, .sensor_type = SENSOR_TYPE_L0X,
      .sensor_index = 4,  .ok = false },   /* 144° — back-right */
    { .mux_addr = 0x70, .mux_channel = 2, .sensor_type = SENSOR_TYPE_L0X,
      .sensor_index = 5,  .ok = false },   /* 180° — back center */
    { .mux_addr = 0x70, .mux_channel = 3, .sensor_type = SENSOR_TYPE_L0X,
      .sensor_index = 6,  .ok = false },   /* 216° — back-left */
    { .mux_addr = 0x70, .mux_channel = 4, .sensor_type = SENSOR_TYPE_L0X,
      .sensor_index = 7,  .ok = false },   /* 252° — left-back */
};

/* Track whether init succeeded */
static bool s_sensor_ready = false;

/* How many sensors initialized — used for single-sensor test mode */
static int s_active_count = 0;

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

#ifdef SINGLE_SENSOR_BROADCAST
    if (s_active_count == 1) {
        ESP_LOGI(TAG, "*** SINGLE SENSOR BROADCAST: all motors mirror one sensor ***");
    }
#endif

    while (1) {
        /* ── Step 1: Read each sensor via its mux channel ──────────── */
        for (int i = 0; i < NUM_ACTIVE_SENSORS; i++) {
            if (!s_sensors[i].ok) continue;

            /* Select this sensor's mux channel (disables the other mux) */
            pca9548a_select(s_sensors[i].mux_addr, s_sensors[i].mux_channel);

            /* Check if this sensor has a new measurement ready.
             * Use the appropriate driver based on sensor type. */
            bool ready = false;
            if (s_sensors[i].sensor_type == SENSOR_TYPE_L1X) {
                vl53l1x_data_ready(&ready);
            } else {
                vl53l0x_data_ready(&ready);
            }

            if (ready) {
                uint16_t distance_mm = 0;
                uint8_t range_status = 0;

                if (s_sensors[i].sensor_type == SENSOR_TYPE_L1X) {
                    vl53l1x_read_distance(&distance_mm);
                    vl53l1x_read_range_status(&range_status);
                    vl53l1x_clear_interrupt();
                } else {
                    vl53l0x_read_distance(&distance_mm);
                    vl53l0x_read_range_status(&range_status);
                    vl53l0x_clear_interrupt();
                }

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

        /* ── Step 1.5: Single-sensor broadcast mode (optional) ──────── *
         * When enabled via #define SINGLE_SENSOR_BROADCAST and only 1
         * sensor is active, copy its distance to ALL 12 pipeline slots
         * so every motor responds. Useful for testing all motors with
         * one sensor. Comment out the #define to test normal angular
         * mapping (1 sensor → only its nearby motors). */
#ifdef SINGLE_SENSOR_BROADCAST
        if (s_active_count == 1) {
            float single_dist = 999.0f;
            for (int i = 0; i < NUM_ACTIVE_SENSORS; i++) {
                if (s_sensors[i].ok) {
                    single_dist = s_distances_cm[s_sensors[i].sensor_index];
                    break;
                }
            }
            for (int i = 0; i < PIPELINE_TOTAL_SENSORS; i++) {
                s_distances_cm[i] = single_dist;
            }
        }
#endif

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
                ESP_LOGI(TAG, "  mux 0x%02X ch%d (%s) → index %d: %6.1f cm",
                         s_sensors[i].mux_addr, s_sensors[i].mux_channel,
                         s_sensors[i].sensor_type == SENSOR_TYPE_L1X ? "L1X" : "L0X",
                         idx, s_distances_cm[idx]);
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

    /* ── Step 3: Create shared I2C device handle at 0x29 ─────────── *
     * Both VL53L0X and VL53L1X use the same I2C address. We create
     * one device handle and share it between both drivers. The mux
     * ensures only one physical sensor is active at a time. */
    i2c_master_dev_handle_t sensor_dev;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SENSOR_I2C_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    rc = i2c_master_bus_add_device(bus, &dev_cfg, &sensor_dev);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add sensor device to I2C bus: %s",
                 esp_err_to_name(rc));
        return false;
    }
    ESP_LOGI(TAG, "Sensor device added to bus (addr=0x%02X)", SENSOR_I2C_ADDR);

    /* Give both drivers access to the shared device handle */
    vl53l1x_set_device(sensor_dev);
    vl53l0x_set_device(sensor_dev);

    /* ── Step 4: Initialize each sensor on its mux channel ───────── */
    int ok_count = 0;
    int l1x_count = 0, l0x_count = 0;

    for (int i = 0; i < NUM_ACTIVE_SENSORS; i++) {
        /* Select this sensor's mux channel */
        pca9548a_select(s_sensors[i].mux_addr, s_sensors[i].mux_channel);
        vTaskDelay(pdMS_TO_TICKS(10));  /* brief settle time after mux switch */

        bool init_ok = false;
        const char *type_str;

        if (s_sensors[i].sensor_type == SENSOR_TYPE_L1X) {
            type_str = "VL53L1X";
            if (vl53l1x_sensor_init() == VL53L1X_OK &&
                vl53l1x_start_ranging() == VL53L1X_OK) {
                init_ok = true;
                l1x_count++;
            }
        } else {
            type_str = "VL53L0X";
            if (vl53l0x_sensor_init() == VL53L0X_OK &&
                vl53l0x_start_ranging() == VL53L0X_OK) {
                init_ok = true;
                l0x_count++;
            }
        }

        s_sensors[i].ok = init_ok;
        if (init_ok) {
            ok_count++;
            ESP_LOGI(TAG, "mux 0x%02X ch%d (%s) → pipeline index %d — OK",
                     s_sensors[i].mux_addr, s_sensors[i].mux_channel,
                     type_str, s_sensors[i].sensor_index);
        } else {
            ESP_LOGW(TAG, "mux 0x%02X ch%d (%s) init FAILED",
                     s_sensors[i].mux_addr, s_sensors[i].mux_channel,
                     type_str);
        }
    }

    s_active_count = ok_count;
    s_sensor_ready = (ok_count > 0);
    if (s_sensor_ready) {
        ESP_LOGI(TAG, "%d/%d sensors ready (%d L1X, %d L0X)",
                 ok_count, NUM_ACTIVE_SENSORS, l1x_count, l0x_count);
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
