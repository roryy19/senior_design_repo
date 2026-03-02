#include "shift_register_packer.h"

namespace firmware {

void packMotorLevels(const uint8_t motorLevels[NUM_MOTORS], uint8_t output[SHIFT_REGISTER_BYTES]) {
    // Build a 32-bit accumulator with each motor's 3 bits at the right position.
    // Motor 0 at bits 23-21, Motor 1 at bits 20-18, ... Motor 7 at bits 2-0.
    uint32_t packed = 0;

    for (int i = 0; i < NUM_MOTORS; i++) {
        uint8_t val = motorLevels[i] & 0x07; // clamp to 3 bits
        int shift = (NUM_MOTORS - 1 - i) * MOTOR_BITS;
        packed |= (static_cast<uint32_t>(val) << shift);
    }

    // Extract 3 bytes, MSB first
    output[0] = static_cast<uint8_t>((packed >> 16) & 0xFF);
    output[1] = static_cast<uint8_t>((packed >> 8)  & 0xFF);
    output[2] = static_cast<uint8_t>((packed >> 0)  & 0xFF);
}

} // namespace firmware
