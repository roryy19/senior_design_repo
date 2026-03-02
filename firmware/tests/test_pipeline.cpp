#include "test_helpers.h"
#include "../src/core/pipeline.h"
#include "../src/core/sensor_config.h"

using namespace firmware;

void test_pipeline_no_obstacles() {
    float sensors[TOTAL_SENSORS];
    for (int i = 0; i < TOTAL_SENSORS; i++) sensors[i] = 200.0f;

    uint8_t shiftData[3];
    uint8_t motors[NUM_MOTORS];
    processSensorReadings(sensors, shiftData, motors);

    for (int m = 0; m < NUM_MOTORS; m++) {
        ASSERT_EQ(0, motors[m], "No obstacles: all motors off");
    }
    ASSERT_HEX_EQ(0x00, shiftData[0], "No obstacles: byte 0 zero");
    ASSERT_HEX_EQ(0x00, shiftData[1], "No obstacles: byte 1 zero");
    ASSERT_HEX_EQ(0x00, shiftData[2], "No obstacles: byte 2 zero");
}

void test_pipeline_close_front_obstacle() {
    float sensors[TOTAL_SENSORS];
    for (int i = 0; i < TOTAL_SENSORS; i++) sensors[i] = 200.0f;
    sensors[0] = 5.0f; // Very close front obstacle -> level 7

    uint8_t shiftData[3];
    uint8_t motors[NUM_MOTORS];
    processSensorReadings(sensors, shiftData, motors);

    // Motor 0 should be 7 (max)
    ASSERT_EQ(7, motors[0], "Front motor max for close obstacle");

    // Byte 0 top 3 bits should be 111 (motor 0 = 7)
    ASSERT_TRUE((shiftData[0] & 0xE0) == 0xE0, "Shift byte 0 top 3 bits set for motor 0");
}

void test_pipeline_mixed_scenario() {
    // Obstacle ahead-right (sensor 1 at 36 deg) and behind (sensor 5 at 180 deg)
    float sensors[TOTAL_SENSORS];
    for (int i = 0; i < TOTAL_SENSORS; i++) sensors[i] = 200.0f;
    sensors[1] = 45.0f; // 36 degrees, level 4
    sensors[5] = 15.0f; // 180 degrees, level 6

    uint8_t shiftData[3];
    uint8_t motors[NUM_MOTORS];
    processSensorReadings(sensors, shiftData, motors);

    // Motor 1 (45 deg, close to sensor 1 at 36 deg) should be active
    ASSERT_TRUE(motors[1] >= 3, "Motor 1 should be active from nearby sensor");
    // Motor 4 (180 deg) should be 6
    ASSERT_EQ(6, motors[4], "Rear motor should be level 6");
    // Output should be non-zero
    ASSERT_TRUE(shiftData[0] != 0 || shiftData[1] != 0 || shiftData[2] != 0,
                "Shift output should be non-zero");
}

void test_pipeline_null_motor_levels() {
    // Test that passing nullptr for motorLevelsOut doesn't crash
    float sensors[TOTAL_SENSORS];
    for (int i = 0; i < TOTAL_SENSORS; i++) sensors[i] = 30.0f;

    uint8_t shiftData[3];
    processSensorReadings(sensors, shiftData, nullptr);

    // Should produce non-zero output without crashing
    ASSERT_TRUE(shiftData[0] != 0 || shiftData[1] != 0 || shiftData[2] != 0,
                "Pipeline with nullptr motorLevels should still produce output");
}

void test_pipeline_with_arm_length() {
    // With longer arm, 110cm should trigger detection
    float sensors[TOTAL_SENSORS];
    for (int i = 0; i < TOTAL_SENSORS; i++) sensors[i] = 200.0f;
    sensors[0] = 110.0f;

    uint8_t shiftData[3];
    uint8_t motors[NUM_MOTORS];

    // Default: 110cm should be level 0
    processSensorReadings(sensors, shiftData, motors, 0.0f);
    ASSERT_EQ(0, motors[0], "110cm default arm: motor off");

    // With 80cm arm: 110cm should trigger
    processSensorReadings(sensors, shiftData, motors, 80.0f);
    ASSERT_TRUE(motors[0] > 0, "110cm with 80cm arm: motor should be active");
}

void runPipelineTests() {
    RUN_TEST(test_pipeline_no_obstacles);
    RUN_TEST(test_pipeline_close_front_obstacle);
    RUN_TEST(test_pipeline_mixed_scenario);
    RUN_TEST(test_pipeline_null_motor_levels);
    RUN_TEST(test_pipeline_with_arm_length);
}
