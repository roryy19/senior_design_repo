/*
 * beacon_scanner.h — BLE beacon scanning and proximity detection
 *
 * Scans for known BLE beacons (XIAO nRF52840 advertisers), smooths RSSI
 * readings, and triggers alerts when a beacon enters proximity.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BEACON_MAC_LEN       6
#define MAX_KNOWN_BEACONS    8
#define RSSI_WINDOW_SIZE     5
#define RSSI_THRESHOLD       (-70)   /* Alert when smoothed RSSI > this (closer = higher) */
#define RSSI_LEAVE_THRESHOLD (-75)   /* Must drop below this to re-arm (hysteresis) */
#define BEACON_COOLDOWN_MS   1000    /* Don't re-alert same beacon for 1 second */
#define BEACON_GONE_TIMEOUT_MS 2000  /* Mark beacon as "left" if not seen for 2s */

/* A known beacon entry. MAC is stored in little-endian (matching NimBLE addr.val[]). */
typedef struct {
    uint8_t mac[BEACON_MAC_LEN];
} known_beacon_t;

/* Per-beacon tracking state */
typedef struct {
    int8_t   rssi_buf[RSSI_WINDOW_SIZE];
    uint8_t  rssi_idx;
    uint8_t  rssi_count;
    int64_t  last_alert_time_ms;
    int64_t  last_seen_time_ms;
    bool     currently_near;
} beacon_state_t;

/*
 * Callback invoked when a beacon enters proximity.
 * mac_le: 6-byte MAC in little-endian (NimBLE format).
 * beacon_index: index into the known_beacons array.
 */
typedef void (*beacon_alert_cb_t)(const uint8_t *mac_le, int beacon_index);

/* Set the callback for beacon proximity alerts. Must be called before beacon_scanner_start. */
void beacon_scanner_set_alert_cb(beacon_alert_cb_t cb);

/*
 * Add a beacon MAC to the known list at runtime.
 * mac_le: 6-byte MAC in little-endian (NimBLE byte order).
 * Returns the beacon index, or -1 if the list is full.
 */
int beacon_scanner_add_beacon(const uint8_t *mac_le);

/* Get the current number of known beacons */
int beacon_scanner_get_count(void);

/* Start the beacon scan FreeRTOS task. Call once from app_main after BLE is initialized. */
void beacon_scanner_start(void);

#ifdef __cplusplus
}
#endif
