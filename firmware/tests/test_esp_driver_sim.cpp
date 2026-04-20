/*
 * test_esp_driver_sim.cpp
 *
 * Simulates the real ESP32 shift_register_send driver exactly —
 * bytes shifted in reverse order, LSB-first within each byte, then
 * latch. Decodes the resulting 24 Q outputs into 8 motor levels and
 * asserts every motor matches the expected level.
 *
 * Mirrors TEST 4.2 in main.c: all 8 motors at the same level 1..7.
 * If this passes, the bit/byte/latch logic is correct and any real-
 * hardware failure is electrical (power, continuity, signal integrity).
 */

#include "test_helpers.h"
#include "../src/core/shift_register_packer.h"
#include "../src/core/sensor_config.h"
#include <cstdint>
#include <cstdio>

using namespace firmware;

/* Simulated 74HC595 chain.
 * shiftReg[0]  = QA of nearest chip (last bit clocked)
 * shiftReg[23] = QH of farthest chip (first bit clocked)
 *
 * Motor i occupies positions (3i, 3i+1, 3i+2) with MSB at the lowest
 * position (matches shift_register.h comment: MSB on lower Q#).
 */
static int shiftReg[24];

static void sim_reset(void) {
    for (int i = 0; i < 24; i++) shiftReg[i] = 0;
}

/* Mirror shift_register_send() exactly: reverse byte order, LSB-first
 * within each byte. On each simulated rising SCLK edge, shift the
 * array down (position 23 drops off) and insert the new bit at [0]. */
static void sim_shift_register_send(const uint8_t *data, int num_bytes) {
    for (int i = num_bytes - 1; i >= 0; i--) {
        for (int bit = 0; bit <= 7; bit++) {
            int new_bit = (data[i] >> bit) & 0x01;
            for (int r = 23; r > 0; r--) {
                shiftReg[r] = shiftReg[r - 1];
            }
            shiftReg[0] = new_bit;
        }
    }
    /* LATCH pulse — in our sim the output register is just shiftReg[]. */
}

/* Decode 24 Q outputs into 8 motor levels.
 * motor i: MSB = shiftReg[3i], mid = shiftReg[3i+1], LSB = shiftReg[3i+2]. */
static void decode_motors(uint8_t motors_out[NUM_MOTORS]) {
    for (int m = 0; m < NUM_MOTORS; m++) {
        int base = m * 3;
        motors_out[m] = (uint8_t)((shiftReg[base] << 2) |
                                  (shiftReg[base + 1] << 1) |
                                   shiftReg[base + 2]);
    }
}

/* Build a 24-bit pattern where all 8 motors are set to the same level. */
static void all_motors_level(uint8_t level, uint8_t packed[SHIFT_REGISTER_BYTES]) {
    uint8_t motors[NUM_MOTORS];
    for (int m = 0; m < NUM_MOTORS; m++) motors[m] = level;
    packMotorLevels(motors, packed);
}

/* --- Individual-motor patterns for TEST 4-style walk --- */

static void test_sim_all_motors_each_level(void) {
    printf("\n  === Sim: all 8 motors at each level 1..7 (mirrors TEST 4.2) ===\n");

    /* Expected packed bytes from main.c comments */
    const uint8_t expected_packed[7][3] = {
        {0x24, 0x92, 0x49}, // level 1
        {0x49, 0x24, 0x92}, // level 2
        {0x6D, 0xB6, 0xDB}, // level 3
        {0x92, 0x49, 0x24}, // level 4
        {0xB6, 0xDB, 0x6D}, // level 5
        {0xDB, 0x6D, 0xB6}, // level 6
        {0xFF, 0xFF, 0xFF}, // level 7
    };

    for (uint8_t lvl = 1; lvl <= 7; lvl++) {
        uint8_t packed[SHIFT_REGISTER_BYTES];
        all_motors_level(lvl, packed);

        /* First sanity check: packer output matches what TEST 4.2 expects. */
        char msg[64];
        snprintf(msg, sizeof(msg), "packer byte[0] @ level %u", lvl);
        ASSERT_HEX_EQ(expected_packed[lvl - 1][0], packed[0], msg);
        snprintf(msg, sizeof(msg), "packer byte[1] @ level %u", lvl);
        ASSERT_HEX_EQ(expected_packed[lvl - 1][1], packed[1], msg);
        snprintf(msg, sizeof(msg), "packer byte[2] @ level %u", lvl);
        ASSERT_HEX_EQ(expected_packed[lvl - 1][2], packed[2], msg);

        /* Run driver sim and decode Q pins back to motor levels. */
        sim_reset();
        sim_shift_register_send(packed, SHIFT_REGISTER_BYTES);

        uint8_t motors_out[NUM_MOTORS];
        decode_motors(motors_out);

        printf("  level %u  packed=0x%02X 0x%02X 0x%02X  decoded=",
               lvl, packed[0], packed[1], packed[2]);
        for (int m = 0; m < NUM_MOTORS; m++) {
            printf("M%d=%u ", m, motors_out[m]);
        }
        printf("\n");

        for (int m = 0; m < NUM_MOTORS; m++) {
            snprintf(msg, sizeof(msg), "motor %d @ level %u", m, lvl);
            ASSERT_EQ(lvl, motors_out[m], msg);
        }
    }
}

static void test_sim_single_motor_walk(void) {
    printf("\n  === Sim: each motor 0..7 at level 7, others 0 ===\n");

    for (int active = 0; active < NUM_MOTORS; active++) {
        uint8_t motors[NUM_MOTORS] = {0};
        motors[active] = 7;

        uint8_t packed[SHIFT_REGISTER_BYTES];
        packMotorLevels(motors, packed);

        sim_reset();
        sim_shift_register_send(packed, SHIFT_REGISTER_BYTES);

        uint8_t motors_out[NUM_MOTORS];
        decode_motors(motors_out);

        printf("  motor %d=7  packed=0x%02X 0x%02X 0x%02X  decoded=",
               active, packed[0], packed[1], packed[2]);
        for (int m = 0; m < NUM_MOTORS; m++) {
            printf("M%d=%u ", m, motors_out[m]);
        }
        printf("\n");

        char msg[64];
        for (int m = 0; m < NUM_MOTORS; m++) {
            uint8_t want = (m == active) ? 7 : 0;
            snprintf(msg, sizeof(msg), "motor %d when motor %d active", m, active);
            ASSERT_EQ(want, motors_out[m], msg);
        }
    }
}

static void test_sim_single_motor_all_levels(void) {
    printf("\n  === Sim: motor 0 at every level 0..7 (mirrors TEST 4) ===\n");

    for (uint8_t lvl = 0; lvl <= 7; lvl++) {
        uint8_t motors[NUM_MOTORS] = {0};
        motors[0] = lvl;

        uint8_t packed[SHIFT_REGISTER_BYTES];
        packMotorLevels(motors, packed);

        sim_reset();
        sim_shift_register_send(packed, SHIFT_REGISTER_BYTES);

        uint8_t motors_out[NUM_MOTORS];
        decode_motors(motors_out);

        char msg[64];
        snprintf(msg, sizeof(msg), "motor 0 @ level %u readback", lvl);
        ASSERT_EQ(lvl, motors_out[0], msg);

        for (int m = 1; m < NUM_MOTORS; m++) {
            snprintf(msg, sizeof(msg), "motor %d stays 0 when motor 0 @ level %u", m, lvl);
            ASSERT_EQ(0, motors_out[m], msg);
        }
    }
}

void runEspDriverSimTests() {
    RUN_TEST(test_sim_all_motors_each_level);
    RUN_TEST(test_sim_single_motor_walk);
    RUN_TEST(test_sim_single_motor_all_levels);
}
