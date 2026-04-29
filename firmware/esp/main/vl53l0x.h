/*
 * vl53l0x.h — Minimal VL53L0X time-of-flight sensor driver for ESP-IDF
 *
 * The VL53L0X is an older ToF sensor from ST that uses 8-bit register
 * addressing (unlike the VL53L1X which uses 16-bit). Both share I2C
 * address 0x29, so they work with the same I2C device handle when
 * multiplexed.
 *
 * Usage with mux (multi-sensor setup):
 *   1. Create I2C device at 0x29 on the bus
 *   2. Call vl53l0x_set_device(dev_handle)
 *   3. For each VL53L0X sensor: select mux channel, call vl53l0x_sensor_init()
 *   4. In read loop: select mux channel, check data_ready, read_distance
 *
 * Init sequence based on ST's VL53L0X API and Pololu VL53L0X library.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VL53L0X_OK      =  0,
    VL53L0X_ERROR   = -1,
    VL53L0X_TIMEOUT = -2,
} vl53l0x_err_t;

/*
 * Set the I2C device handle for VL53L0X operations.
 *
 * Both VL53L0X and VL53L1X share address 0x29. The caller creates
 * one device handle and passes it to both drivers. The mux ensures
 * only one physical sensor is active at a time.
 */
void vl53l0x_set_device(i2c_master_dev_handle_t dev);

/*
 * Run the full initialization sequence on the currently-selected sensor.
 *
 * Performs: model ID check, data init, tuning settings, SPAD configuration,
 * reference calibration, and GPIO interrupt config.
 *
 * Select the mux channel BEFORE calling this. Call once per physical sensor.
 * Requires vl53l0x_set_device() to have been called first.
 */
vl53l0x_err_t vl53l0x_sensor_init(void);

/*
 * Start continuous ranging measurements (back-to-back mode).
 *
 * After this, call vl53l0x_data_ready() + vl53l0x_read_distance() in a loop.
 * The sensor measures as fast as possible (~33ms per measurement).
 */
vl53l0x_err_t vl53l0x_start_ranging(void);

/*
 * Check if a new measurement is available.
 * Sets *ready to true if new data is available.
 */
vl53l0x_err_t vl53l0x_data_ready(bool *ready);

/*
 * Read the latest distance measurement in millimeters.
 * Only valid after vl53l0x_data_ready() returns true.
 */
vl53l0x_err_t vl53l0x_read_distance(uint16_t *distance_mm);

/*
 * Read the range status of the latest measurement.
 *   0 = Valid measurement
 *   1 = Sigma failure
 *   2 = Signal failure
 *   3 = Min range / wrap
 *   4 = Phase failure
 */
vl53l0x_err_t vl53l0x_read_range_status(uint8_t *status);

/*
 * Clear the data-ready interrupt. Must call after each read.
 */
vl53l0x_err_t vl53l0x_clear_interrupt(void);

#ifdef __cplusplus
}
#endif
