/*
 * Obstacle Detection Belt — ESP32-S3 Firmware
 *
 * BLE peripheral (phone connects to belt) + BLE observer (scans for beacons).
 * When a known beacon enters proximity, the belt:
 *   1. Plays a stored TTS audio clip through the speaker (always)
 *   2. Sends a beacon alert to the phone via BLE notify (if connected)
 *
 * Audio clips are stored in SPIFFS flash, sent from the phone during setup.
 * The belt operates independently at runtime — phone connection is optional.
 *
 * Flash with:   idf.py -p COMx flash monitor
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "beacon_scanner.h"
#include "audio_player.h"
#include "clip_storage.h"
#include "sensor_task.h"

static const char *TAG = "BELT_BLE";

/* GPIO pin for audio PWM output (connect to RC filter -> LM386 input) */
#define AUDIO_GPIO GPIO_NUM_1

/* ---------- UUIDs (must match src/ble/uuids.ts in the phone app) ----------
 *
 * 128-bit UUIDs are stored in LITTLE-ENDIAN byte order for NimBLE.
 * The UUID string "4fafc201-1fb5-459e-8fcc-c5c9c331914b" becomes
 * the byte array reversed: {0x4b, 0x91, 0x31, 0xc3, ...}
 */

/* Service: 4fafc201-1fb5-459e-8fcc-c5c9c331914b */
static const ble_uuid128_t BELT_SERVICE_UUID =
    BLE_UUID128_INIT(0x4b, 0x91, 0x31, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f,
                     0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f);

/* Alert characteristic: beb5483e-36e1-4688-b7f5-ea07361b26a8 (notify) */
static const ble_uuid128_t ALERT_CHAR_UUID =
    BLE_UUID128_INIT(0xa8, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
                     0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);

/* Config characteristic: beb5483e-36e1-4688-b7f5-ea07361b26a9 (write) */
static const ble_uuid128_t CONFIG_CHAR_UUID =
    BLE_UUID128_INIT(0xa9, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
                     0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);

/* ---------- BLE connection state ---------- */

static uint16_t conn_handle = 0;
static bool connected = false;
static uint16_t alert_attr_handle = 0;
static bool notify_enabled = false;

/* ---------- Audio clip receive buffer ---------- */

/* Temporary buffer for receiving audio chunks from the phone.
 * Chunks arrive via CONFIG_CHAR writes and are assembled here,
 * then written to SPIFFS when the "audio end" command is received. */
static uint8_t audio_recv_buf[MAX_CLIP_SIZE];
static size_t  audio_recv_len = 0;
static uint8_t audio_recv_mac[BEACON_MAC_LEN]; /* MAC of clip being received */
static bool    audio_recv_active = false;

/* Playback buffer — loaded from SPIFFS when a beacon is detected */
static uint8_t playback_buf[MAX_CLIP_SIZE];

/* ---------- Send beacon alert to phone ---------- */

static void send_beacon_alert(const uint8_t *mac_le)
{
    if (!connected || !notify_enabled) return;

    /* Build alert packet: [0x01, MAC_B0..B5] in big-endian (phone expects this) */
    uint8_t alert[7];
    alert[0] = 0x01;           /* type = beacon */
    alert[1] = mac_le[5];     /* Reverse NimBLE LE → phone's expected BE */
    alert[2] = mac_le[4];
    alert[3] = mac_le[3];
    alert[4] = mac_le[2];
    alert[5] = mac_le[1];
    alert[6] = mac_le[0];

    struct os_mbuf *om = ble_hs_mbuf_from_flat(alert, sizeof(alert));
    int rc = ble_gatts_notify_custom(conn_handle, alert_attr_handle, om);

    if (rc == 0) {
        ESP_LOGI(TAG, "Sent beacon alert: MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                 alert[1], alert[2], alert[3], alert[4], alert[5], alert[6]);
    } else {
        ESP_LOGW(TAG, "Failed to send beacon alert, rc=%d", rc);
    }
}

/* ---------- Send RSSI update to phone ---------- */

void send_rssi_update(const uint8_t *mac_le, int8_t rssi)
{
    if (!connected || !notify_enabled) return;

    /* Build RSSI update packet: [0x03, MAC_BE[6], RSSI_offset] */
    uint8_t pkt[8];
    pkt[0] = 0x03;           /* type = RSSI update */
    pkt[1] = mac_le[5];     /* Reverse NimBLE LE → phone's expected BE */
    pkt[2] = mac_le[4];
    pkt[3] = mac_le[3];
    pkt[4] = mac_le[2];
    pkt[5] = mac_le[1];
    pkt[6] = mac_le[0];
    pkt[7] = (uint8_t)((int16_t)rssi + 128);  /* Offset encoding: -127→1, 0→128 */

    struct os_mbuf *om = ble_hs_mbuf_from_flat(pkt, sizeof(pkt));
    ble_gatts_notify_custom(conn_handle, alert_attr_handle, om);
}

/* ---------- Beacon proximity callback ---------- */

/* Called by beacon_scanner when a known beacon enters proximity.
 * Plays local audio AND sends BLE alert to phone (if connected). */
static void on_beacon_detected(const uint8_t *mac_le, int beacon_index)
{
    ESP_LOGI(TAG, "Beacon %d detected! Playing audio + notifying phone", beacon_index);

    /* 1. Play local audio from flash (always, regardless of phone connection) */
    if (clip_storage_exists(mac_le)) {
        size_t clip_len = clip_storage_read(mac_le, playback_buf, sizeof(playback_buf));
        if (clip_len > 0) {
            audio_player_play(playback_buf, clip_len);
        }
    } else {
        ESP_LOGW(TAG, "No audio clip stored for beacon %d", beacon_index);
    }

    /* 2. Send BLE alert to phone (only if connected and subscribed) */
    send_beacon_alert(mac_le);
}

/* ---------- Config characteristic write handler ---------- */

/*
 * Config command types (phone → belt):
 *   0x01: Arm length      [0x01, N]
 *   0x02: Audio chunk      [0x02, MAC[6], IDX_hi, IDX_lo, PCM_DATA...]
 *   0x03: Audio end        [0x03, MAC[6]]
 *   0x04: Register beacon  [0x04, MAC[6]]
 *   0x05: Delete beacon    [0x05, MAC[6]]
 *   0x06: RSSI threshold   [0x06, THRESHOLD_OFFSET]  (offset = value + 128)
 *   0x07: Clear all         [0x07]  (remove all beacons + audio clips — phone will re-sync)
 */
static int config_write_cb(uint16_t conn_handle_arg, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t buf[256];
    if (len > sizeof(buf)) len = sizeof(buf);
    os_mbuf_copydata(ctxt->om, 0, len, buf);

    if (len < 1) return 0;

    switch (buf[0]) {

    case 0x01: /* Arm length */
        if (len >= 2) {
            ESP_LOGI(TAG, "Received arm length from phone: %d cm", buf[1]);
        }
        break;

    case 0x02: /* Audio chunk */
        if (len < 9) { /* 1 type + 6 MAC + 2 chunk index = 9 min */
            ESP_LOGW(TAG, "Audio chunk too short (%d bytes)", len);
            break;
        }
        {
            uint8_t *mac = &buf[1];
            uint16_t chunk_idx = (buf[7] << 8) | buf[8];
            uint8_t *pcm_data = &buf[9];
            size_t pcm_len = len - 9;

            /* If this is a new transfer (first chunk or different MAC), reset buffer.
             * Compare in big-endian since that's what arrives from the phone. */
            uint8_t mac_le[BEACON_MAC_LEN];
            mac_le[0] = mac[5]; mac_le[1] = mac[4]; mac_le[2] = mac[3];
            mac_le[3] = mac[2]; mac_le[4] = mac[1]; mac_le[5] = mac[0];

            if (!audio_recv_active ||
                memcmp(audio_recv_mac, mac_le, BEACON_MAC_LEN) != 0) {
                memcpy(audio_recv_mac, mac_le, BEACON_MAC_LEN);
                audio_recv_len = 0;
                audio_recv_active = true;
                ESP_LOGI(TAG, "Starting audio receive for MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }

            /* Append PCM data to receive buffer */
            if (audio_recv_len + pcm_len <= MAX_CLIP_SIZE) {
                memcpy(&audio_recv_buf[audio_recv_len], pcm_data, pcm_len);
                audio_recv_len += pcm_len;
                ESP_LOGD(TAG, "Audio chunk %d: %d bytes (total %d)",
                         chunk_idx, (int)pcm_len, (int)audio_recv_len);
            } else {
                ESP_LOGW(TAG, "Audio buffer overflow at chunk %d", chunk_idx);
            }
        }
        break;

    case 0x03: /* Audio end */
        if (len >= 7 && audio_recv_active) {
            /* Write accumulated audio to flash */
            if (clip_storage_write(audio_recv_mac, audio_recv_buf, audio_recv_len)) {
                ESP_LOGI(TAG, "Audio clip stored: %d bytes (%.1fs)",
                         (int)audio_recv_len, (float)audio_recv_len / 8000.0f);

                /* Automatically add this MAC to the beacon scan list */
                beacon_scanner_add_beacon(audio_recv_mac);
            }
            audio_recv_active = false;
            audio_recv_len = 0;
        }
        break;

    case 0x04: /* Register beacon MAC (no audio, just add to scan list) */
        if (len >= 7) {
            uint8_t mac_le[BEACON_MAC_LEN];
            /* Convert big-endian (phone) → little-endian (NimBLE) */
            mac_le[0] = buf[6];
            mac_le[1] = buf[5];
            mac_le[2] = buf[4];
            mac_le[3] = buf[3];
            mac_le[4] = buf[2];
            mac_le[5] = buf[1];
            beacon_scanner_add_beacon(mac_le);
        }
        break;

    case 0x06: /* Set RSSI threshold */
        if (len >= 2) {
            int8_t threshold = (int8_t)(buf[1] - 128);
            beacon_scanner_set_threshold(threshold);
        }
        break;

    case 0x05: /* Delete beacon (remove from scan list + delete audio clip) */
        if (len >= 7) {
            uint8_t mac_le[BEACON_MAC_LEN];
            mac_le[0] = buf[6];
            mac_le[1] = buf[5];
            mac_le[2] = buf[4];
            mac_le[3] = buf[3];
            mac_le[4] = buf[2];
            mac_le[5] = buf[1];
            beacon_scanner_remove_beacon(mac_le);
            clip_storage_delete(mac_le);
        }
        break;

    case 0x07: /* Clear all beacons + audio clips (phone will re-sync after this) */
        ESP_LOGI(TAG, "Clearing all beacons and audio clips (phone sync)");
        beacon_scanner_clear_all();
        clip_storage_clear_all();
        break;

    default:
        ESP_LOGI(TAG, "Received unknown config type 0x%02X (%d bytes)", buf[0], len);
        break;
    }

    return 0;
}

/* ---------- Alert characteristic read handler ---------- */

static int alert_read_cb(uint16_t conn_handle_arg, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* No static data to return — alerts are push-only via notify */
    return 0;
}

/* ---------- GATT service definition ---------- */

static const struct ble_gatt_svc_def gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &BELT_SERVICE_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Alert characteristic — phone subscribes to notifications */
                .uuid = &ALERT_CHAR_UUID.u,
                .access_cb = alert_read_cb,
                .val_handle = &alert_attr_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                /* Config characteristic — phone writes config and audio data */
                .uuid = &CONFIG_CHAR_UUID.u,
                .access_cb = config_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            { 0 } /* terminator */
        },
    },
    { 0 } /* terminator */
};

/* ---------- Forward declarations ---------- */

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg);

/* ---------- Advertising ---------- */

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};
    struct ble_hs_adv_fields rsp_fields = {0};

    /* Advertising packet: flags + 128-bit service UUID (21 bytes, fits in 31) */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &BELT_SERVICE_UUID;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv fields, rc=%d", rc);
        return;
    }

    /* Scan response packet: device name (fits separately) */
    const char *name = ble_svc_gap_device_name();
    rsp_fields.name = (uint8_t *)name;
    rsp_fields.name_len = strlen(name);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set scan response fields, rc=%d", rc);
        return;
    }

    /* Connectable, undirected advertising */
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                      &adv_params, ble_gap_event_handler, NULL);

    ESP_LOGD(TAG, "Advertising started");
}

/* Called by beacon_scanner.c to restart advertising after a scan window */
void restart_advertising(void)
{
    if (!connected) {
        start_advertising();
    }
}

/* ---------- GAP event handler ---------- */

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            connected = true;
            ESP_LOGI(TAG, "Phone connected! (handle=%d)", conn_handle);
        } else {
            ESP_LOGW(TAG, "Connection failed, status=%d", event->connect.status);
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        connected = false;
        notify_enabled = false;
        ESP_LOGI(TAG, "Phone disconnected, reason=%d — re-advertising...",
                 event->disconnect.reason);
        start_advertising();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == alert_attr_handle) {
            notify_enabled = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "Phone %s alert notifications",
                     notify_enabled ? "subscribed to" : "unsubscribed from");
        }
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;
    }

    return 0;
}

/* ---------- NimBLE host task + sync callback ---------- */

static void on_ble_sync(void)
{
    /* Generate a public address if one doesn't exist */
    ble_hs_util_ensure_addr(0);

    start_advertising();
}

static void on_ble_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset, reason=%d", reason);
}

static void nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ---------- Entry point ---------- */

void app_main(void)
{
    ESP_LOGI(TAG, "=== Obstacle Detection Belt Firmware ===");

    /* Initialize non-volatile storage (required by BLE stack) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Initialize audio clip storage (SPIFFS) */
    clip_storage_init();

    /* Initialize audio player (PWM output) */
    audio_player_init(AUDIO_GPIO);

    /* Initialize distance sensor (I2C bus + VL53L1X boot sequence) */
    sensor_task_init();

    /* Initialize the NimBLE host */
    nimble_port_init();

    ble_hs_cfg.sync_cb = on_ble_sync;
    ble_hs_cfg.reset_cb = on_ble_reset;

    /* Register GATT services */
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_services);
    ble_gatts_add_svcs(gatt_services);

    /* Set the device name */
    ble_svc_gap_device_name_set("ObstacleBelt");

    /* Start the NimBLE host task */
    nimble_port_freertos_init(nimble_host_task);

    /* Set up beacon scanner callback and start the scan task */
    beacon_scanner_set_alert_cb(on_beacon_detected);
    beacon_scanner_start();

    /* Start distance sensor reading task */
    sensor_task_start();

    ESP_LOGI(TAG, "Belt initialized. Scanning for beacons + waiting for phone.");
}
