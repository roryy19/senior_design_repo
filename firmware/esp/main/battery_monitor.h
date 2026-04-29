/*
 * battery_monitor.h — ADC-based battery voltage monitor for the belt.
 *
 * Inputs:
 *   GPIO 4 (ADC1_CH3): voltage-divider output from the 12V rail.
 *   Expected mapping: 3.0 V ADC = 12 V full. (≈ 10.2 V rail = 2.55 V ADC.)
 *
 * Outputs:
 *   GPIO 11, GPIO 12: enable/shutdown lines to the two buck converters.
 *                     Driven HIGH to shut a converter off.
 *
 * Behavior:
 *   - Sample once per second.
 *   - After 5 consecutive samples below 2.55 V: send a single BLE notify
 *     [0x04] to the phone (battery-low popup). Hysteresis clears at 2.60 V.
 *   - After 5 consecutive samples below 2.50 V: pull GPIO 11 + 12 HIGH
 *     (disable both bucks), stop the task. One-way — recover by power cycle.
 *
 * Gate: the feature is DISABLED by default (BATTERY_MONITOR_ENABLED = 0) so
 * it cannot fire spuriously when the divider hardware isn't wired. Flip the
 * macro to 1 once the hardware is in place.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Set to 1 ONLY after the voltage divider + buck shutdown lines are wired. */
#define BATTERY_MONITOR_ENABLED 0

/* Start the battery monitor task. Called from app_main under the
 * BATTERY_MONITOR_ENABLED guard — do not call unconditionally. */
void battery_monitor_start(void);

/* Implemented in main.c. Sends [0x04] notify to phone if connected. */
void send_battery_alert(void);

#ifdef __cplusplus
}
#endif
