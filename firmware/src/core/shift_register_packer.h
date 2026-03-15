#pragma once
#include <cstdint>
#include "sensor_config.h"

namespace firmware {

// Pack 8 motor levels (each 3 bits, values 0-7) into 3 bytes (24 bits).
//
// Bit layout (MSB first, shifted out first):
//   byte[0] bits 7-5 = motor 0, bits 4-2 = motor 1, bits 1-0 = motor 2 (upper 2)
//   byte[1] bit 7 = motor 2 (lower 1), bits 6-4 = motor 3, bits 3-1 = motor 4, bit 0 = motor 5 (upper 1)
//   byte[2] bits 7-6 = motor 5 (lower 2), bits 5-3 = motor 6, bits 2-0 = motor 7
void packMotorLevels(const uint8_t motorLevels[NUM_MOTORS], uint8_t output[SHIFT_REGISTER_BYTES]);

} // namespace firmware
