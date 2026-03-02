#include "shift_register_output.h"
#include <cstdio>

namespace firmware {

void shiftOut(const uint8_t data[SHIFT_REGISTER_BYTES]) {
    // Simulated GPIO pin states
    bool DATA_PIN  = false;
    bool SCLK_PIN  = false;
    bool LATCH_PIN = false;

    // Simulated shift register contents (24 positions)
    int shiftReg[24] = {0};
    int regSize = 0;

    int totalBits = SHIFT_REGISTER_BYTES * 8; // 24

    printf("\n=== Shift Register Output Begin ===\n");
    printf("LATCH_PIN = LOW (holding previous output stable)\n\n");

    for (int byteIdx = 0; byteIdx < SHIFT_REGISTER_BYTES; byteIdx++) {
        printf("--- Shifting byte %d: 0x%02X (%d%d%d%d%d%d%d%d) ---\n",
               byteIdx, data[byteIdx],
               (data[byteIdx] >> 7) & 1, (data[byteIdx] >> 6) & 1,
               (data[byteIdx] >> 5) & 1, (data[byteIdx] >> 4) & 1,
               (data[byteIdx] >> 3) & 1, (data[byteIdx] >> 2) & 1,
               (data[byteIdx] >> 1) & 1, (data[byteIdx] >> 0) & 1);

        for (int bit = 7; bit >= 0; bit--) {
            int bitNum = byteIdx * 8 + (7 - bit); // 0-based bit counter

            // Set DATA pin to current bit value
            DATA_PIN = (data[byteIdx] >> bit) & 0x01;

            // Pulse SCLK: rising edge captures the bit
            SCLK_PIN = true;

            // Shift everything in the register down by 1, insert new bit at front
            for (int r = regSize; r > 0; r--) {
                shiftReg[r] = shiftReg[r - 1];
            }
            shiftReg[0] = DATA_PIN ? 1 : 0;
            if (regSize < totalBits) regSize++;

            printf("  Bit %2d: DATA=%d  SCLK=HIGH(capture)  Register: [",
                   bitNum, DATA_PIN ? 1 : 0);
            for (int r = 0; r < regSize; r++) {
                printf("%d", shiftReg[r]);
                // Show motor boundaries every 3 bits
                if ((totalBits - regSize + r + 1) % 3 == 0 && r < regSize - 1) {
                    printf("|");
                }
            }
            printf("]\n");

            // SCLK goes low, ready for next bit
            SCLK_PIN = false;
        }
        printf("\n");
    }

    // Pulse LATCH: transfer shift register to output register
    printf("--- All %d bits shifted. Pulsing latch. ---\n", totalBits);
    LATCH_PIN = true;
    printf("LATCH_PIN = HIGH (transferring to output register)\n");

    printf("\nOutput register now holds:\n");
    printf("  ");
    // Read oldest-first (the first bit shifted in is at the far end)
    for (int r = totalBits - 1; r >= 0; r--) {
        printf("%d", shiftReg[r]);
        if ((totalBits - r) % 3 == 0 && r > 0) {
            printf(" ");
        }
    }
    printf("\n  ");
    for (int m = 0; m < NUM_MOTORS; m++) {
        // Motor 0 = first 3 bits shifted in = positions [23, 22, 21] in register
        int base = totalBits - 1 - (m * 3);
        int val = (shiftReg[base] << 2) | (shiftReg[base - 1] << 1) | shiftReg[base - 2];
        printf("M%d=%d ", m, val);
    }
    printf("\n");

    LATCH_PIN = false;
    printf("LATCH_PIN = LOW (output latched, motors running)\n");
    printf("=== Shift Register Output Complete ===\n\n");
}

} // namespace firmware
