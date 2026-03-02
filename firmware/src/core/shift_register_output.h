#pragma once
#include <cstdint>
#include "sensor_config.h"

namespace firmware {

// Simulated shift register output.
// Shifts out packed bytes one bit at a time with simulated DATA, SCLK, and LATCH
// signals. Prints each step to the terminal for verification.
void shiftOut(const uint8_t data[SHIFT_REGISTER_BYTES]);

} // namespace firmware
