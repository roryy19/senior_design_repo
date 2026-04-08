/*
 * distance_categorizer_wrapper.cpp — Bridge from C to C++ categorizer
 *
 * This file MUST be .cpp (not .c) because it #includes the C++ header
 * that uses "namespace firmware". The ESP-IDF build system automatically
 * compiles .cpp files with the C++ compiler and .c files with the C compiler.
 *
 * The "extern C" on our function gives it C linkage, so sensor_task.c
 * can call it even though this file is compiled as C++.
 */

#include "distance_categorizer_wrapper.h"
#include "distance_categorizer.h"  /* from firmware/src/core/ (C++, namespace firmware) */

extern "C" uint8_t categorize_distance_cm(float distance_cm)
{
    /* Call the C++ function. We pass 0.0f for armLengthCm to use
     * the default (unscaled) thresholds. Later, when the phone app
     * sends the user's arm length via BLE, we can pass it here. */
    return firmware::categorizeDistance(distance_cm, 0.0f);
}
