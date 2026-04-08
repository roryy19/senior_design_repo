/*
 * sensor_task.h — Distance sensor reading task
 *
 * Creates a FreeRTOS task that continuously reads the VL53L1X sensor,
 * converts the distance to a motor vibration level (0-7), and logs both
 * values to the serial monitor.
 *
 * Usage in app_main():
 *   sensor_task_init();   // set up I2C + sensor hardware
 *   sensor_task_start();  // launch the background reading task
 *
 * This follows the same pattern as beacon_scanner.h in this project.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize the VL53L1X sensor (I2C bus + sensor boot + configuration).
 * Call once from app_main, before sensor_task_start().
 *
 * Returns true if sensor initialized successfully, false on error.
 * On failure, sensor_task_start() will be a no-op (safe to call).
 */
bool sensor_task_init(void);

/*
 * Start the sensor reading FreeRTOS task.
 * Call once from app_main, after sensor_task_init().
 *
 * The task runs at priority 5 (higher than beacon scanner at 3),
 * reading the sensor every ~50ms and logging:
 *   Distance: 45.2 cm | Motor level: 4/7
 */
void sensor_task_start(void);

#ifdef __cplusplus
}
#endif
