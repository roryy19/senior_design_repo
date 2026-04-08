/*
 * distance_categorizer_wrapper.h — C-callable bridge to the C++ distance categorizer
 *
 * WHY THIS FILE EXISTS:
 *
 * The distance categorizer (firmware/src/core/distance_categorizer.h) is written
 * in C++ and lives inside "namespace firmware". Our main.c is plain C, and C
 * doesn't understand C++ namespaces.
 *
 * This wrapper provides a plain C function that internally calls the C++ version.
 * The "extern C" block tells the C++ compiler to give the function C-style linkage
 * so that C code can call it.
 *
 * Call chain:
 *   sensor_task.c (C)  →  categorize_distance_cm()  →  firmware::categorizeDistance() (C++)
 *                          ^^^ this wrapper ^^^          ^^^ existing core library ^^^
 *
 * The extern "C" guard pattern here matches beacon_scanner.h and other headers
 * in this project.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert a raw distance (in cm) to a 3-bit motor vibration level (0–7).
 *
 *   Level 0 = far away (>= 100 cm), motor off
 *   Level 7 = very close (< 10 cm), maximum vibration
 *
 * Thresholds (from sensor_config.h):
 *   >= 100 cm → 0    >= 55 cm → 3    >= 10 cm → 6
 *   >=  85 cm → 1    >= 40 cm → 4    <  10 cm → 7
 *   >=  70 cm → 2    >= 25 cm → 5
 */
uint8_t categorize_distance_cm(float distance_cm);

#ifdef __cplusplus
}
#endif
