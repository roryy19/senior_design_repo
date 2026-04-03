/*
 * BLE Beacon Firmware for Seeed XIAO nRF52840
 *
 * This sketch turns the XIAO into a simple BLE beacon that broadcasts
 * continuously. The ESP32-S3 belt scans for this beacon's MAC address
 * and triggers an audio alert when the user is nearby.
 *
 * Setup:
 *   1. Install Seeed nRF52840 board package in Arduino IDE
 *      (Board Manager URL: https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json)
 *   2. Select board: Seeed XIAO nRF52840
 *   3. Upload this sketch
 *
 * To discover the MAC address:
 *   Flash this sketch, power the XIAO on near the ESP32, and read the
 *   MAC from the ESP32's serial monitor output. iOS hides real MACs,
 *   so the ESP32 scan log is the reliable method.
 *
 * Change BEACON_NAME below to give each beacon a unique name for
 * identification during MAC discovery (e.g., "OB_STAIRS", "OB_DOOR").
 */

#include <bluefruit.h>

/* ---------- Configuration ---------- */

// Change this for each beacon so you can tell them apart in the ESP32 scan log.
// Keep it short (max ~10 characters) to fit in the advertising packet.
#define BEACON_NAME "OB_STAIRS"

// Advertising interval in units of 0.625ms.
// 320 = 200ms — fast updates for near-real-time RSSI tracking.
// Uses more power, but beacons are wall-powered so this is fine.
#define ADV_INTERVAL 320

void setup()
{
    // Initialize the Bluefruit BLE stack
    Bluefruit.begin();

    // Set transmit power (range: -40, -20, -16, -12, -8, -4, 0, 2, 3, 4, 5, 6, 7, 8 dBm)
    // 4 dBm gives good range (~10m) while being power-efficient.
    Bluefruit.setTxPower(4);

    // Set the device name (visible in scan results for identification)
    Bluefruit.setName(BEACON_NAME);

    // Configure advertising
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addName();

    // Non-connectable: the beacon just broadcasts, never accepts connections.
    // This saves power and simplifies the firmware.
    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);

    // Restart advertising automatically if it ever stops (shouldn't happen, but safety net)
    Bluefruit.Advertising.restartOnDisconnect(true);

    // Set advertising interval (same value for min and max = fixed interval)
    Bluefruit.Advertising.setInterval(ADV_INTERVAL, ADV_INTERVAL);

    // Start advertising forever (0 = no timeout)
    Bluefruit.Advertising.start(0);
}

void loop()
{
    // Nothing to do — the BLE stack handles advertising in the background.
    // Use low-power delay to save battery.
    delay(10000);
}
