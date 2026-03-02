#include "test_helpers.h"
#include "../src/core/pipeline.h"
#include "../src/core/shift_register_output.h"
#include "../src/core/sensor_config.h"
#include <cstdio>

using namespace firmware;

void test_full_simulation_two_obstacles() {
    printf("\n  === Scenario: Wall ahead (30cm) + Table behind-right (20cm) ===\n");

    // Set up sensor readings
    float sensors[TOTAL_SENSORS];
    for (int i = 0; i < TOTAL_SENSORS; i++) sensors[i] = 200.0f;
    sensors[0] = 30.0f;  // Wall ahead
    sensors[4] = 20.0f;  // Table at 144 degrees

    // Run pipeline
    uint8_t shiftData[SHIFT_REGISTER_BYTES];
    uint8_t motors[NUM_MOTORS];
    processSensorReadings(sensors, shiftData, motors);

    // Print motor levels
    printf("\n  Motor levels after pipeline:\n  ");
    const char* names[] = {"Front", "F-Right", "Right", "R-Right", "Rear", "R-Left", "Left", "F-Left"};
    for (int m = 0; m < NUM_MOTORS; m++) {
        printf("  M%d(%s)=%d", m, names[m], motors[m]);
    }
    printf("\n\n  Packed bytes: 0x%02X 0x%02X 0x%02X\n", shiftData[0], shiftData[1], shiftData[2]);

    // Shift out with simulated hardware
    shiftOut(shiftData);

    ASSERT_TRUE(motors[0] == 5, "Front motor should be 5 (wall)");
    ASSERT_TRUE(motors[3] == 5, "R-Right motor should be 5 (table)");
    ASSERT_TRUE(motors[4] == 4, "Rear motor should be 4 (table spread)");
}

void test_full_simulation_no_obstacles() {
    printf("\n  === Scenario: All clear (no obstacles) ===\n");

    float sensors[TOTAL_SENSORS];
    for (int i = 0; i < TOTAL_SENSORS; i++) sensors[i] = 200.0f;

    uint8_t shiftData[SHIFT_REGISTER_BYTES];
    uint8_t motors[NUM_MOTORS];
    processSensorReadings(sensors, shiftData, motors);

    printf("\n  Motor levels: all zeros\n");
    printf("  Packed bytes: 0x%02X 0x%02X 0x%02X\n", shiftData[0], shiftData[1], shiftData[2]);

    shiftOut(shiftData);

    ASSERT_HEX_EQ(0x00, shiftData[0], "All clear: byte 0");
    ASSERT_HEX_EQ(0x00, shiftData[1], "All clear: byte 1");
    ASSERT_HEX_EQ(0x00, shiftData[2], "All clear: byte 2");
}

void test_full_simulation_immediate_front() {
    printf("\n  === Scenario: Obstacle 5cm directly ahead ===\n");

    float sensors[TOTAL_SENSORS];
    for (int i = 0; i < TOTAL_SENSORS; i++) sensors[i] = 200.0f;
    sensors[0] = 5.0f;

    uint8_t shiftData[SHIFT_REGISTER_BYTES];
    uint8_t motors[NUM_MOTORS];
    processSensorReadings(sensors, shiftData, motors);

    printf("\n  Motor levels:\n  ");
    const char* names[] = {"Front", "F-Right", "Right", "R-Right", "Rear", "R-Left", "Left", "F-Left"};
    for (int m = 0; m < NUM_MOTORS; m++) {
        printf("  M%d(%s)=%d", m, names[m], motors[m]);
    }
    printf("\n\n  Packed bytes: 0x%02X 0x%02X 0x%02X\n", shiftData[0], shiftData[1], shiftData[2]);

    shiftOut(shiftData);

    ASSERT_EQ(7, motors[0], "Front motor max for immediate danger");
}

void runShiftRegisterOutputTests() {
    RUN_TEST(test_full_simulation_two_obstacles);
    RUN_TEST(test_full_simulation_no_obstacles);
    RUN_TEST(test_full_simulation_immediate_front);
}
