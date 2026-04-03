/*
 * beacon_scanner.c — BLE beacon scanning and proximity detection
 *
 * Runs a FreeRTOS task that periodically scans for BLE advertisements.
 * When a known beacon's smoothed RSSI exceeds the proximity threshold,
 * invokes the registered alert callback.
 */

#include "beacon_scanner.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"

/* Defined in main.c — we call these from the scan task/callback */
extern void restart_advertising(void);
extern void send_rssi_update(const uint8_t *mac_le, int8_t rssi);

static const char *TAG = "BEACON_SCAN";

/* ---------- State ---------- */

static known_beacon_t known_beacons[MAX_KNOWN_BEACONS];
static beacon_state_t beacon_states[MAX_KNOWN_BEACONS];
static int num_known_beacons = 0;

static beacon_alert_cb_t alert_cb = NULL;

/* ---------- Public API ---------- */

void beacon_scanner_set_alert_cb(beacon_alert_cb_t cb)
{
    alert_cb = cb;
}

int beacon_scanner_add_beacon(const uint8_t *mac_le)
{
    /* Check if already in the list */
    for (int i = 0; i < num_known_beacons; i++) {
        if (memcmp(known_beacons[i].mac, mac_le, BEACON_MAC_LEN) == 0) {
            ESP_LOGI(TAG, "Beacon %d already registered: MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                     i, mac_le[5], mac_le[4], mac_le[3],
                     mac_le[2], mac_le[1], mac_le[0]);
            return i;
        }
    }

    if (num_known_beacons >= MAX_KNOWN_BEACONS) {
        ESP_LOGW(TAG, "Known beacon list full (%d max)", MAX_KNOWN_BEACONS);
        return -1;
    }

    int idx = num_known_beacons;
    memcpy(known_beacons[idx].mac, mac_le, BEACON_MAC_LEN);
    memset(&beacon_states[idx], 0, sizeof(beacon_state_t));
    num_known_beacons++;

    /* Log in human-readable big-endian order */
    ESP_LOGI(TAG, "Added beacon %d: MAC=%02X:%02X:%02X:%02X:%02X:%02X",
             idx,
             mac_le[5], mac_le[4], mac_le[3],
             mac_le[2], mac_le[1], mac_le[0]);

    return idx;
}

bool beacon_scanner_remove_beacon(const uint8_t *mac_le)
{
    for (int i = 0; i < num_known_beacons; i++) {
        if (memcmp(known_beacons[i].mac, mac_le, BEACON_MAC_LEN) == 0) {
            ESP_LOGI(TAG, "Removed beacon %d: MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                     i, mac_le[5], mac_le[4], mac_le[3],
                     mac_le[2], mac_le[1], mac_le[0]);

            /* Shift remaining beacons down to fill the gap */
            for (int j = i; j < num_known_beacons - 1; j++) {
                known_beacons[j] = known_beacons[j + 1];
                beacon_states[j] = beacon_states[j + 1];
            }
            num_known_beacons--;
            memset(&beacon_states[num_known_beacons], 0, sizeof(beacon_state_t));
            return true;
        }
    }
    return false;
}

int beacon_scanner_get_count(void)
{
    return num_known_beacons;
}

/* ---------- RSSI smoothing ---------- */

static void rssi_add_reading(int idx, int8_t rssi)
{
    beacon_state_t *bs = &beacon_states[idx];
    bs->rssi_buf[bs->rssi_idx] = rssi;
    bs->rssi_idx = (bs->rssi_idx + 1) % RSSI_WINDOW_SIZE;
    if (bs->rssi_count < RSSI_WINDOW_SIZE) {
        bs->rssi_count++;
    }
}

static int8_t rssi_get_average(int idx)
{
    beacon_state_t *bs = &beacon_states[idx];
    if (bs->rssi_count == 0) return -127;

    int32_t sum = 0;
    for (int i = 0; i < bs->rssi_count; i++) {
        sum += bs->rssi_buf[i];
    }
    return (int8_t)(sum / bs->rssi_count);
}

/* ---------- Scan callback ---------- */

static int scan_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    struct ble_gap_disc_desc *desc = &event->disc;

    /* Check if this device matches any known beacon */
    for (int i = 0; i < num_known_beacons; i++) {
        if (memcmp(desc->addr.val, known_beacons[i].mac, BEACON_MAC_LEN) != 0) {
            continue;
        }

        rssi_add_reading(i, desc->rssi);
        int8_t avg = rssi_get_average(i);

        int64_t now_ms = esp_timer_get_time() / 1000;
        beacon_state_t *bs = &beacon_states[i];
        bs->last_seen_time_ms = now_ms;

        /* Send RSSI update to phone */
        send_rssi_update(known_beacons[i].mac, avg);

        if (avg > RSSI_THRESHOLD) {
            if (!bs->currently_near &&
                (now_ms - bs->last_alert_time_ms > BEACON_COOLDOWN_MS)) {
                /* Beacon just entered proximity — fire alert */
                bs->currently_near = true;
                bs->last_alert_time_ms = now_ms;

                ESP_LOGI(TAG, "Beacon %d entered proximity (avg RSSI=%d)", i, avg);

                if (alert_cb) {
                    alert_cb(known_beacons[i].mac, i);
                }
            }
        } else if (avg < RSSI_LEAVE_THRESHOLD) {
            /* Hysteresis: must drop 5 dBm below entry threshold to "leave".
             * Prevents alert spam when user is right at the boundary. */
            if (bs->currently_near) {
                ESP_LOGI(TAG, "Beacon %d left proximity (avg RSSI=%d)", i, avg);
            }
            bs->currently_near = false;
        }

        break; /* Found matching beacon, done */
    }

    return 0;
}

/* ---------- Timeout check ---------- */

static void check_beacon_timeouts(void)
{
    int64_t now_ms = esp_timer_get_time() / 1000;

    for (int i = 0; i < num_known_beacons; i++) {
        beacon_state_t *bs = &beacon_states[i];

        if (bs->currently_near && bs->last_seen_time_ms > 0 &&
            (now_ms - bs->last_seen_time_ms > BEACON_GONE_TIMEOUT_MS)) {
            bs->currently_near = false;
            bs->rssi_count = 0;
            bs->rssi_idx = 0;
            ESP_LOGI(TAG, "Beacon %d left proximity (timeout — no signal)", i);
        }
    }
}

/* ---------- Scan task ---------- */

static void beacon_scan_task(void *param)
{
    /* Brief wait for BLE stack to fully initialize */
    vTaskDelay(pdMS_TO_TICKS(1000));

    struct ble_gap_disc_params scan_params = {0};
    scan_params.passive = 1;            /* Passive scan — no SCAN_REQ sent */
    scan_params.itvl = 160;             /* Scan interval: 100ms (160 * 0.625ms) */
    scan_params.window = 160;           /* Scan window:  100ms — 100% duty cycle */
    scan_params.filter_duplicates = 0;  /* Need repeated adverts for RSSI tracking */
    scan_params.limited = 0;

    ESP_LOGI(TAG, "Beacon scan task started (%d known beacons)", num_known_beacons);

    while (1) {
        if (ble_hs_synced()) {
            int rc = ble_gap_disc(
                BLE_OWN_ADDR_PUBLIC,
                1500,                    /* Duration: 1500ms */
                &scan_params,
                scan_gap_event_handler,
                NULL
            );

            if (rc == 0) {
                ESP_LOGD(TAG, "Scan started");
            } else if (rc == BLE_HS_EALREADY) {
                ESP_LOGD(TAG, "Scan already in progress");
            } else if (rc == BLE_HS_EBUSY) {
                /* Advertising blocks scanning on classic BLE.
                 * Stop advertising, scan, then restart. */
                ble_gap_adv_stop();
                rc = ble_gap_disc(
                    BLE_OWN_ADDR_PUBLIC, 1500,
                    &scan_params, scan_gap_event_handler, NULL);
                if (rc != 0) {
                    ESP_LOGW(TAG, "Scan failed after adv stop, rc=%d", rc);
                }
            } else {
                ESP_LOGW(TAG, "Failed to start scan, rc=%d", rc);
            }
        }

        /* Wait for the 1.5s scan to complete, then restart advertising */
        vTaskDelay(pdMS_TO_TICKS(1700));
        ble_gap_disc_cancel();
        check_beacon_timeouts();
        restart_advertising();

        /* Brief pause before next scan cycle */
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

void beacon_scanner_start(void)
{
    xTaskCreate(beacon_scan_task, "beacon_scan", 4096, NULL, 3, NULL);
}
