/*
 * pca9548a.h — PCA9548A I2C multiplexer driver
 *
 * This project uses TWO PCA9548A multiplexers on the same I2C bus
 * (GPIO 8 SDA, GPIO 9 SCL) to connect 12 VL53L1X distance sensors:
 *
 *   MUX0 (address 0x70): 5 sensors (VL53L0X-type, channels 0-4)
 *   MUX1 (address 0x71): 7 sensors (VL53L1X-type, channels 0-6)
 *
 * All sensors share I2C address 0x29. The mux selects which physical
 * sensor is electrically connected to the bus at any given time.
 *
 * IMPORTANT: Only ONE channel across BOTH muxes may be active at a
 * time. If two sensors at 0x29 are on the bus simultaneously, I2C
 * traffic will collide. pca9548a_select() enforces this by disabling
 * the other mux before enabling the requested channel.
 *
 * How the PCA9548A works:
 *   - Write a single byte to its I2C address
 *   - Each bit enables/disables a channel (bit 0 = ch0, bit 1 = ch1, etc.)
 *   - Write 0x00 = all channels disabled
 *   - Write 0x01 = only channel 0 enabled
 *   - Write 0x04 = only channel 2 enabled
 */

#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* I2C addresses of the two PCA9548A multiplexers.
 * A0 pin determines the LSB: MUX0 has A0=GND (0x70), MUX1 has A0=VCC (0x71). */
#define PCA9548A_MUX0_ADDR  0x70
#define PCA9548A_MUX1_ADDR  0x71

/*
 * Initialize both PCA9548A multiplexers on the given I2C bus.
 * Adds device handles for both 0x70 and 0x71.
 * Call once after I2C bus creation, before using pca9548a_select().
 */
esp_err_t pca9548a_init(i2c_master_bus_handle_t bus);

/*
 * Select a single channel on a specific multiplexer.
 *
 * This function:
 *   1. Disables ALL channels on the OTHER mux (prevents address collision)
 *   2. Enables the requested channel on the target mux
 *
 * After this call, I2C traffic to address 0x29 reaches only the sensor
 * connected to the selected mux/channel.
 *
 * @param mux_addr  PCA9548A_MUX0_ADDR (0x70) or PCA9548A_MUX1_ADDR (0x71)
 * @param channel   Channel number (0-7)
 */
esp_err_t pca9548a_select(uint8_t mux_addr, uint8_t channel);

/*
 * Disable all channels on both multiplexers.
 * No sensor is reachable on the I2C bus after this call.
 */
esp_err_t pca9548a_disable_all(void);

#ifdef __cplusplus
}
#endif
