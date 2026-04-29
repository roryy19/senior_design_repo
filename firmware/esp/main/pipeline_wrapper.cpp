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

/* Set by main.c 0x01 config handler when the phone sends the user's
 * shoulder-to-fingertip length. 0.0f means "use unscaled defaults". */
static float s_arm_length_cm = 0.0f;

extern "C" void pipeline_set_arm_length(float cm) {
    s_arm_length_cm = cm;
}

extern "C" void pipeline_process(
    const float sensor_distances_cm[PIPELINE_TOTAL_SENSORS],
    uint8_t shift_reg_out[PIPELINE_SHIFT_REG_BYTES],
    uint8_t motor_levels_out[PIPELINE_NUM_MOTORS])
{
    firmware::processSensorReadings(
        sensor_distances_cm,
        shift_reg_out,
        motor_levels_out,
        s_arm_length_cm
    );
}
