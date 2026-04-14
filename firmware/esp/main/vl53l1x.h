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
 * Supports two modes:
 *   1. Single-sensor: call vl53l1x_init() (creates bus + device + inits sensor)
 *   2. Multi-sensor via mux: call vl53l1x_create_on_bus() once, then
 *      vl53l1x_sensor_init() for each sensor after selecting its mux channel
 *
 * All sensors share I2C address 0x29. When using a multiplexer, switch
 * the mux channel before any vl53l1x_* call to talk to the right sensor.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VL53L1X_OK      =  0,
    VL53L1X_ERROR   = -1,
    VL53L1X_TIMEOUT = -2,
} vl53l1x_err_t;

/*
 * Initialize I2C bus + VL53L1X sensor (single-sensor convenience).
 *
 * What this does (Arduino equivalent in parentheses):
 *   1. Creates the I2C master bus on GPIO 8/9 at 400kHz  (Wire.begin(8, 9))
 *   2. Adds device at address 0x29
 *   3. Runs full sensor init (boot wait, config, calibration, mode setup)
 *
 * This is a convenience wrapper. For multi-sensor setups with a mux,
 * use vl53l1x_create_on_bus() + vl53l1x_sensor_init() instead.
 *
 * Call once from app_main, before vl53l1x_start_ranging().
 * Returns VL53L1X_OK on success, or an error code if the sensor isn't found.
 */
vl53l1x_err_t vl53l1x_init(void);

/*
 * Add the VL53L1X device to an existing I2C bus (for mux configurations).
 *
 * Call this ONCE after creating the I2C bus and initializing the mux.
 * This only registers the device at address 0x29 on the bus — it does
 * NOT run the sensor init sequence.
 *
 * After this, select a mux channel and call vl53l1x_sensor_init() for
 * each physical sensor.
 *
 * @param bus  I2C master bus handle (created externally)
 */
vl53l1x_err_t vl53l1x_create_on_bus(i2c_master_bus_handle_t bus);

/*
 * Set the I2C device handle directly (alternative to vl53l1x_create_on_bus).
 *
 * Use this when the caller creates the device handle externally (e.g.,
 * when sharing a device handle between VL53L1X and VL53L0X drivers).
 * Both sensor types share I2C address 0x29.
 */
void vl53l1x_set_device(i2c_master_dev_handle_t dev);

/*
 * Run the sensor initialization sequence on the currently-selected sensor.
 *
 * Performs: model ID check, firmware boot wait, default config write,
 * calibration measurement, Long mode + 50ms timing budget setup.
 *
 * For multi-sensor setups: select the mux channel BEFORE calling this.
 * Call once per physical sensor.
 *
 * Requires that either vl53l1x_init() or vl53l1x_create_on_bus() was
 * called first (device handle must exist).
 */
vl53l1x_err_t vl53l1x_sensor_init(void);

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
