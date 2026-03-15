#pragma once
#include <cstdint>
#include "sensor_config.h"

namespace firmware {

// Full processing pipeline: 12 raw sensor readings -> 3 packed bytes for shift register.
//
// motorLevelsOut: optional output for intermediate motor levels (for debugging/BLE).
//                Pass nullptr to skip.
void processSensorReadings(
    const float sensorDistancesCm[TOTAL_SENSORS],
    uint8_t shiftRegisterOut[SHIFT_REGISTER_BYTES],
    uint8_t motorLevelsOut[NUM_MOTORS] = nullptr,
    float armLengthCm = 0.0f
);

} // namespace firmware
