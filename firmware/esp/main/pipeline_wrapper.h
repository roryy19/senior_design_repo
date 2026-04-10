/*
 * pipeline_wrapper.h — C-callable bridge to the C++ sensor pipeline
 *
 * WHY THIS FILE EXISTS:
 *
 * The full processing pipeline (firmware/src/core/pipeline.h) is C++ with
 * namespaces. Our sensor_task.c is plain C. This wrapper provides a C
 * function that internally calls the C++ pipeline.
 *
 * Call chain:
 *   sensor_task.c (C) → pipeline_process() → firmware::processSensorReadings() (C++)
 *                        ^^^ this wrapper ^^^   ^^^ existing core library ^^^
 *
 * The pipeline does:
 *   12 raw distances → motor_mapper (12→8 motors) → packer (8 levels→3 bytes)
 *
 * This follows the same pattern as distance_categorizer_wrapper.h.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* These match the constants in sensor_config.h (C++ namespace).
 * Duplicated here so C code can use them without including C++ headers. */
#define PIPELINE_TOTAL_SENSORS      12
#define PIPELINE_NUM_MOTORS          8
#define PIPELINE_SHIFT_REG_BYTES     3

/*
 * Run the full sensor-to-motor pipeline.
 *
 * Takes 12 raw distance readings and produces:
 *   1. 3 packed bytes ready for shift_register_send()
 *   2. Optionally, 8 motor levels (0-7) for debugging/logging
 *
 * Sensors that are not physically connected should have their distance
 * set to a large value (e.g., 999.0f) — this categorizes to level 0
 * (motor off), so unused sensors don't affect motor output.
 *
 * @param sensor_distances_cm  Array of 12 floats: 10 belt sensors + 2 front sensors.
 *                             Index order matches sensor_config.h layout.
 * @param shift_reg_out        Output: 3 bytes for shift_register_send().
 * @param motor_levels_out     Output: 8 motor levels (0-7). Pass NULL to skip.
 */
void pipeline_process(
    const float sensor_distances_cm[PIPELINE_TOTAL_SENSORS],
    uint8_t shift_reg_out[PIPELINE_SHIFT_REG_BYTES],
    uint8_t motor_levels_out[PIPELINE_NUM_MOTORS]
);

#ifdef __cplusplus
}
#endif
