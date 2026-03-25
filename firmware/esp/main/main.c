/*
 * BLE Test Firmware for Obstacle Detection Belt
 *
 * This is a MINIMAL test firmware for the ESP32-S3. It does NOT read sensors
 * or drive motors. It only validates that the phone app can:
 *   1. Find the belt via BLE scan
 *   2. Connect to it
 *   3. Receive alert notifications (a fake beacon alert every 5 seconds)
 *   4. Send config writes (arm length — printed to serial terminal)
 *
 * Flash with:   idf.py -p COMx flash monitor
 * (replace COMx with the actual COM port, e.g. COM3)
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

static const char *TAG = "BELT_BLE";

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

/* ---------- State ---------- */

static uint16_t conn_handle = 0;           /* current connection handle */
static bool connected = false;              /* is a phone connected? */
static uint16_t alert_attr_handle = 0;      /* attribute handle for alert char */
static bool notify_enabled = false;         /* has the phone subscribed to alerts? */

/* ---------- Fake test data ---------- */

/* A fake beacon MAC address: 11:22:33:44:55:66
 * Alert packet format: [0x01, B0, B1, B2, B3, B4, B5]
 * The phone app will look up this MAC in the sensor list. If you add a
 * sensor with MAC "11:22:33:44:55:66" in the app, it will speak that name. */
static const uint8_t fake_beacon_alert[] = {
    0x01,                               /* type = beacon */
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66  /* MAC address */
};

/* ---------- Config characteristic write handler ---------- */

static int config_write_cb(uint16_t conn_handle_arg, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* Read the bytes the phone sent */
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t buf[32];
    if (len > sizeof(buf)) len = sizeof(buf);
    os_mbuf_copydata(ctxt->om, 0, len, buf);

    if (len >= 2 && buf[0] == 0x01) {
        ESP_LOGI(TAG, "Received arm length from phone: %d cm", buf[1]);
    } else {
        ESP_LOGI(TAG, "Received unknown config write (%d bytes)", len);
    }

    return 0;
}

/* ---------- Alert characteristic read handler (required but not used) ---------- */

static int alert_read_cb(uint16_t conn_handle_arg, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* Return the fake beacon alert as the current value */
    os_mbuf_append(ctxt->om, fake_beacon_alert, sizeof(fake_beacon_alert));
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
                /* Config characteristic — phone writes arm length etc. */
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

    ESP_LOGI(TAG, "Advertising started — waiting for phone to connect...");
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

/* ---------- Task: send test alert every 5 seconds ---------- */

static void alert_task(void *param)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        if (connected && notify_enabled) {
            struct os_mbuf *om = ble_hs_mbuf_from_flat(
                fake_beacon_alert, sizeof(fake_beacon_alert));

            int rc = ble_gatts_notify_custom(conn_handle, alert_attr_handle, om);

            if (rc == 0) {
                ESP_LOGI(TAG, "Sent test beacon alert (MAC 11:22:33:44:55:66)");
            } else {
                ESP_LOGW(TAG, "Failed to send alert, rc=%d", rc);
            }
        }
    }
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
    /* This function runs the NimBLE host event loop. It returns only
     * when nimble_port_stop() is called. */
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ---------- Entry point ---------- */

void app_main(void)
{
    ESP_LOGI(TAG, "=== Obstacle Belt BLE Test Firmware ===");

    /* Initialize non-volatile storage (required by BLE stack) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

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

    /* Start the test alert task — sends a fake beacon alert every 5 seconds */
    xTaskCreate(alert_task, "alert_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "BLE initialized. Open the app and tap Connect.");
}
