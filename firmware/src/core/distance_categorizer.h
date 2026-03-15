#pragma once
#include <cstdint>

namespace firmware {

// Convert a raw distance in centimeters to a 3-bit vibration level (0-7).
// Level 0 = no vibration (far away or no valid reading).
// Level 7 = maximum vibration (very close).
//
// armLengthCm: optional user arm length for dynamic threshold scaling.
//              Pass 0.0f or negative to use default (unscaled) thresholds.
uint8_t categorizeDistance(float distanceCm, float armLengthCm = 0.0f);

} // namespace firmware
