#pragma once
#include <cstdint>
#include "sensor_config.h"

namespace firmware {

// Map 12 sensor distance readings to 8 motor vibration levels.
//
// Input array layout:
//   indices [0..9]  = belt sensors at BELT_SENSOR_ANGLES[0..9]
//   index   [10]    = front sensor up
//   index   [11]    = front sensor down
//
// Each motor gets the MAX contribution from nearby sensors, with
// linear angular falloff (full at 0 degrees, half at MAX_SPREAD_ANGLE,
// nothing beyond MAX_SPREAD_ANGLE).
//
// armLengthCm: optional arm length for dynamic threshold scaling.
void mapSensorsToMotors(
    const float sensorDistancesCm[TOTAL_SENSORS],
    uint8_t motorLevels[NUM_MOTORS],
    float armLengthCm = 0.0f
);

} // namespace firmware
