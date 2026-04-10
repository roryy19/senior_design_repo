/*
 * pipeline_wrapper.cpp — Bridge from C to C++ sensor pipeline
 *
 * This file MUST be .cpp (not .c) because it #includes the C++ header
 * that uses "namespace firmware". The ESP-IDF build system automatically
 * compiles .cpp files with the C++ compiler and .c files with the C compiler.
 *
 * The "extern C" on our function gives it C linkage, so sensor_task.c
 * can call it even though this file is compiled as C++.
 */

#include "pipeline_wrapper.h"
#include "pipeline.h"  /* from firmware/src/core/ (C++, namespace firmware) */

extern "C" void pipeline_process(
    const float sensor_distances_cm[PIPELINE_TOTAL_SENSORS],
    uint8_t shift_reg_out[PIPELINE_SHIFT_REG_BYTES],
    uint8_t motor_levels_out[PIPELINE_NUM_MOTORS])
{
    /* Call the C++ pipeline. Pass 0.0f for armLengthCm to use the
     * default (unscaled) thresholds. When the phone sends the user's
     * arm length via BLE config command 0x01, we can update this. */
    firmware::processSensorReadings(
        sensor_distances_cm,
        shift_reg_out,
        motor_levels_out,
        0.0f
    );
}
