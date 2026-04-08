/*
 * vl53l1x.h — Minimal VL53L1X time-of-flight sensor driver for ESP-IDF
 *
 * Replaces the Arduino Wire.h + Pololu VL53L1X library used in the
 * teammate's single-sensor test code. Uses ESP-IDF's I2C master driver
 * to talk to the sensor over I2C (GPIO 8 SDA, GPIO 9 SCL).
 *
 * This driver is self-contained: no external library download needed.
 * The sensor's default configuration blob (from ST's Ultra Lite Driver)
 * is embedded directly in vl53l1x.c.
 *
 * Hardcoded for single-sensor use at I2C address 0x29.
 * Configured for Long distance mode (up to 4m) with 50ms timing budget,
 * matching the teammate's Arduino settings.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VL53L1X_OK      =  0,
    VL53L1X_ERROR   = -1,
    VL53L1X_TIMEOUT = -2,
} vl53l1x_err_t;

/*
 * Initialize I2C bus + VL53L1X sensor.
 *
 * What this does (Arduino equivalent in parentheses):
 *   1. Creates the I2C master bus on GPIO 8/9 at 400kHz  (Wire.begin(8, 9))
 *   2. Waits for sensor firmware to boot                 (automatic in Pololu init)
 *   3. Writes the 91-byte default configuration          (sensor.init())
 *   4. Sets Long distance mode (up to 4m)                (sensor.setDistanceMode(Long))
 *   5. Sets 50ms timing budget                           (sensor.setMeasurementTimingBudget(50000))
 *
 * Call once from app_main, before vl53l1x_start_ranging().
 * Returns VL53L1X_OK on success, or an error code if the sensor isn't found.
 */
vl53l1x_err_t vl53l1x_init(void);

/*
 * Start continuous ranging measurements.
 * (Arduino equivalent: sensor.startContinuous(50))
 *
 * After this, call vl53l1x_data_ready() + vl53l1x_read() in a loop.
 */
vl53l1x_err_t vl53l1x_start_ranging(void);

/*
 * Check if a new measurement is available.
 * (Arduino equivalent: part of sensor.read() which blocks internally)
 *
 * Sets *ready to true if new data is available.
 */
vl53l1x_err_t vl53l1x_data_ready(bool *ready);

/*
 * Read the latest distance measurement in millimeters.
 * (Arduino equivalent: sensor.read() returns mm)
 *
 * Only valid after vl53l1x_data_ready() returns true.
 */
vl53l1x_err_t vl53l1x_read_distance(uint16_t *distance_mm);

/*
 * Read the range status of the latest measurement.
 * (Arduino equivalent: sensor.ranging_data.range_status)
 *
 *   0 = Valid measurement
 *   1 = Sigma failure (too much noise)
 *   2 = Signal failure (too weak return signal)
 *   4 = Out of bounds / phase failure
 *   7 = Wrapped target
 */
vl53l1x_err_t vl53l1x_read_range_status(uint8_t *status);

/*
 * Clear the data-ready interrupt. Must call after each read.
 * (Arduino equivalent: handled internally by Pololu library)
 */
vl53l1x_err_t vl53l1x_clear_interrupt(void);

#ifdef __cplusplus
}
#endif
