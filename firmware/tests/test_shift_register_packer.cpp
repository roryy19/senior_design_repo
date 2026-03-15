#include "test_helpers.h"
#include "../src/core/shift_register_packer.h"

using namespace firmware;

void test_all_zeros() {
    uint8_t motors[NUM_MOTORS] = {0,0,0,0,0,0,0,0};
    uint8_t out[3];
    packMotorLevels(motors, out);
    ASSERT_HEX_EQ(0x00, out[0], "All zeros: byte 0");
    ASSERT_HEX_EQ(0x00, out[1], "All zeros: byte 1");
    ASSERT_HEX_EQ(0x00, out[2], "All zeros: byte 2");
}

void test_all_max() {
    uint8_t motors[NUM_MOTORS] = {7,7,7,7,7,7,7,7};
    uint8_t out[3];
    packMotorLevels(motors, out);
    // 24 bits all 1s = 0xFF 0xFF 0xFF
    ASSERT_HEX_EQ(0xFF, out[0], "All max: byte 0");
    ASSERT_HEX_EQ(0xFF, out[1], "All max: byte 1");
    ASSERT_HEX_EQ(0xFF, out[2], "All max: byte 2");
}

void test_single_motor_0_max() {
    // Motor 0 = 7 (111), rest = 0
    // Bits: 111 000 000 000 000 000 000 000
    // Byte 0: 11100000 = 0xE0
    uint8_t motors[NUM_MOTORS] = {7,0,0,0,0,0,0,0};
    uint8_t out[3];
    packMotorLevels(motors, out);
    ASSERT_HEX_EQ(0xE0, out[0], "Motor 0 max: byte 0");
    ASSERT_HEX_EQ(0x00, out[1], "Motor 0 max: byte 1");
    ASSERT_HEX_EQ(0x00, out[2], "Motor 0 max: byte 2");
}

void test_single_motor_7_max() {
    // Motor 7 = 7 (111), rest = 0
    // Bits: 000 000 000 000 000 000 000 111
    // Byte 2: 00000111 = 0x07
    uint8_t motors[NUM_MOTORS] = {0,0,0,0,0,0,0,7};
    uint8_t out[3];
    packMotorLevels(motors, out);
    ASSERT_HEX_EQ(0x00, out[0], "Motor 7 max: byte 0");
    ASSERT_HEX_EQ(0x00, out[1], "Motor 7 max: byte 1");
    ASSERT_HEX_EQ(0x07, out[2], "Motor 7 max: byte 2");
}

void test_alternating_pattern() {
    // Motors: 5, 0, 5, 0, 5, 0, 5, 0
    // Binary: 101 000 101 000 101 000 101 000
    // Byte 0: 10100010 = 0xA2
    // Byte 1: 10001010 = 0x8A
    // Byte 2: 00101000 = 0x28
    uint8_t motors[NUM_MOTORS] = {5,0,5,0,5,0,5,0};
    uint8_t out[3];
    packMotorLevels(motors, out);
    ASSERT_HEX_EQ(0xA2, out[0], "Alternating: byte 0");
    ASSERT_HEX_EQ(0x8A, out[1], "Alternating: byte 1");
    ASSERT_HEX_EQ(0x28, out[2], "Alternating: byte 2");
}

void test_sequential_pattern() {
    // Motors: 1, 2, 3, 4, 5, 6, 7, 0
    // Binary: 001 010 011 100 101 110 111 000
    // 24 bits: 001010011100101110111000
    // Byte 0: 00101001 = 0x29
    // Byte 1: 11001011 = 0xCB
    // Byte 2: 10111000 = 0xB8
    uint8_t motors[NUM_MOTORS] = {1,2,3,4,5,6,7,0};
    uint8_t out[3];
    packMotorLevels(motors, out);
    ASSERT_HEX_EQ(0x29, out[0], "Sequential: byte 0");
    ASSERT_HEX_EQ(0xCB, out[1], "Sequential: byte 1");
    ASSERT_HEX_EQ(0xB8, out[2], "Sequential: byte 2");
}

void test_single_motor_4_value_3() {
    // Motor 4 = 3 (011), rest = 0
    // Bits: 000 000 000 000 011 000 000 000
    // Position: motor 4 starts at bit (7-4)*3 = 9, so bits 11-9
    // Packed = 0x000600
    // Byte 0: 0x00, Byte 1: 0x06, Byte 2: 0x00
    uint8_t motors[NUM_MOTORS] = {0,0,0,0,3,0,0,0};
    uint8_t out[3];
    packMotorLevels(motors, out);
    ASSERT_HEX_EQ(0x00, out[0], "Motor 4=3: byte 0");
    ASSERT_HEX_EQ(0x06, out[1], "Motor 4=3: byte 1");
    ASSERT_HEX_EQ(0x00, out[2], "Motor 4=3: byte 2");
}

void runShiftRegisterPackerTests() {
    RUN_TEST(test_all_zeros);
    RUN_TEST(test_all_max);
    RUN_TEST(test_single_motor_0_max);
    RUN_TEST(test_single_motor_7_max);
    RUN_TEST(test_alternating_pattern);
    RUN_TEST(test_sequential_pattern);
    RUN_TEST(test_single_motor_4_value_3);
}
