#include "test_helpers.h"
#include "../src/core/motor_mapper.h"
#include "../src/core/sensor_config.h"

using namespace firmware;

// Helper: set all sensors to a given distance
static void fillSensors(float arr[TOTAL_SENSORS], float value) {
    for (int i = 0; i < TOTAL_SENSORS; i++) {
        arr[i] = value;
    }
}

void test_no_obstacles_all_motors_off() {
    float sensors[TOTAL_SENSORS];
    fillSensors(sensors, 200.0f); // everything far away
    uint8_t motors[NUM_MOTORS];
    mapSensorsToMotors(sensors, motors);
    for (int i = 0; i < NUM_MOTORS; i++) {
        ASSERT_EQ(0, motors[i], "All motors should be off with no obstacles");
    }
}

void test_single_front_obstacle() {
    // Belt sensor 0 (0 degrees) detects at 30cm -> level 5
    float sensors[TOTAL_SENSORS];
    fillSensors(sensors, 200.0f);
    sensors[0] = 30.0f;
    uint8_t motors[NUM_MOTORS];
    mapSensorsToMotors(sensors, motors);

    // Motor 0 (0 deg): co-located with sensor 0 -> full level 5
    ASSERT_EQ(5, motors[0], "Front motor should be level 5");

    // Motor 1 (45 deg away): should get falloff (within spread)
    ASSERT_TRUE(motors[1] > 0 && motors[1] < 5, "Motor 1 should get reduced level");

    // Motor 7 (315 deg = 45 deg away wrapping): should get falloff
    ASSERT_TRUE(motors[7] > 0 && motors[7] < 5, "Motor 7 should get reduced level");

    // Motors 2-6: beyond 45 degrees from sensor 0, should be off
    for (int i = 2; i <= 6; i++) {
        ASSERT_EQ(0, motors[i], "Distant motors should be off");
    }
}

void test_single_rear_obstacle() {
    // Belt sensor 5 (180 degrees) detects at 20cm -> level 6
    float sensors[TOTAL_SENSORS];
    fillSensors(sensors, 200.0f);
    sensors[5] = 20.0f;
    uint8_t motors[NUM_MOTORS];
    mapSensorsToMotors(sensors, motors);

    // Motor 4 (180 deg): co-located -> level 6
    ASSERT_EQ(6, motors[4], "Rear motor should be level 6");

    // Motors 3 (135 deg) and 5 (225 deg): 45 deg away, should get falloff
    ASSERT_TRUE(motors[3] > 0, "Motor 3 should get spread from rear sensor");
    ASSERT_TRUE(motors[5] > 0, "Motor 5 should get spread from rear sensor");

    // Motor 0 (front): should be off
    ASSERT_EQ(0, motors[0], "Front motor should be off for rear obstacle");
}

void test_two_independent_obstacles() {
    // Front: sensor 0 at 30cm -> level 5
    // Rear: sensor 5 at 20cm -> level 6
    float sensors[TOTAL_SENSORS];
    fillSensors(sensors, 200.0f);
    sensors[0] = 30.0f;
    sensors[5] = 20.0f;
    uint8_t motors[NUM_MOTORS];
    mapSensorsToMotors(sensors, motors);

    // Front group
    ASSERT_EQ(5, motors[0], "Front motor from front obstacle");
    // Rear group
    ASSERT_EQ(6, motors[4], "Rear motor from rear obstacle");
    // Motor 2 (90 deg) is equidistant from both -- should be off
    ASSERT_EQ(0, motors[2], "Motor 2 should be off between two obstacles");
}

void test_front_up_down_sensors() {
    // Front up detects at 40cm -> level 4
    // Front down detects at 15cm -> level 6
    // MAX should win: motor 0 gets level 6
    float sensors[TOTAL_SENSORS];
    fillSensors(sensors, 200.0f);
    sensors[10] = 40.0f; // front up
    sensors[11] = 15.0f; // front down (closer)
    uint8_t motors[NUM_MOTORS];
    mapSensorsToMotors(sensors, motors);

    ASSERT_TRUE(motors[0] >= 6, "Front motor should reflect closest front sensor");
}

void test_adjacent_sensors_max_rule() {
    // Sensors 0 (0 deg) and 1 (36 deg) both detect
    // Motor 1 (45 deg) gets contributions from both, MAX wins
    float sensors[TOTAL_SENSORS];
    fillSensors(sensors, 200.0f);
    sensors[0] = 50.0f; // level 4
    sensors[1] = 30.0f; // level 5
    uint8_t motors[NUM_MOTORS];
    mapSensorsToMotors(sensors, motors);

    // Motor 1 should be at least 4 from the overlapping contributions
    ASSERT_TRUE(motors[1] >= 4, "Motor 1 should be at least level 4 from adjacent sensors");
}

void test_surrounded_all_motors_high() {
    // Every belt sensor detects at 20cm -> level 6
    float sensors[TOTAL_SENSORS];
    for (int i = 0; i < NUM_BELT_SENSORS; i++) {
        sensors[i] = 20.0f;
    }
    sensors[10] = 200.0f; // front up: far
    sensors[11] = 200.0f; // front down: far
    uint8_t motors[NUM_MOTORS];
    mapSensorsToMotors(sensors, motors);

    for (int m = 0; m < NUM_MOTORS; m++) {
        ASSERT_TRUE(motors[m] >= 5, "All motors should be high when surrounded");
    }
}

void test_immediate_danger_all_close() {
    // Every sensor at 5cm -> level 7
    // Motors not co-located with a sensor get falloff (6 instead of 7)
    // since sensors are at 36° and motors at 45° intervals
    float sensors[TOTAL_SENSORS];
    fillSensors(sensors, 5.0f);
    uint8_t motors[NUM_MOTORS];
    mapSensorsToMotors(sensors, motors);

    for (int m = 0; m < NUM_MOTORS; m++) {
        ASSERT_TRUE(motors[m] >= 6, "All motors should be near max when all sensors very close");
    }
}

void test_sensor_at_36_degrees_spread() {
    // Sensor 1 (36 degrees) detects at 30cm -> level 5
    // Motor 0 (0 deg, 36 deg away): gets falloff
    // Motor 1 (45 deg, 9 deg away): gets near-full
    // Motor 2 (90 deg, 54 deg away): beyond spread, should be 0
    float sensors[TOTAL_SENSORS];
    fillSensors(sensors, 200.0f);
    sensors[1] = 30.0f;
    uint8_t motors[NUM_MOTORS];
    mapSensorsToMotors(sensors, motors);

    ASSERT_TRUE(motors[0] > 0, "Motor 0 should get falloff from sensor at 36 deg");
    ASSERT_TRUE(motors[1] > motors[0], "Motor 1 should be stronger than motor 0 (closer to sensor)");
    ASSERT_EQ(0, motors[2], "Motor 2 should be off (54 deg away from sensor)");
}

void runMotorMapperTests() {
    RUN_TEST(test_no_obstacles_all_motors_off);
    RUN_TEST(test_single_front_obstacle);
    RUN_TEST(test_single_rear_obstacle);
    RUN_TEST(test_two_independent_obstacles);
    RUN_TEST(test_front_up_down_sensors);
    RUN_TEST(test_adjacent_sensors_max_rule);
    RUN_TEST(test_surrounded_all_motors_high);
    RUN_TEST(test_immediate_danger_all_close);
    RUN_TEST(test_sensor_at_36_degrees_spread);
}
