#pragma once
#include <cstdint>

namespace firmware {

// --- Sensor positions ---
// 10 belt sensors at 36-degree intervals (clockwise from front)
constexpr int NUM_BELT_SENSORS = 10;
constexpr float BELT_SENSOR_ANGLES[NUM_BELT_SENSORS] = {
    0.0f, 36.0f, 72.0f, 108.0f, 144.0f,
    180.0f, 216.0f, 252.0f, 288.0f, 324.0f
};

// 2 front sensors (up-angled and down-angled)
// Both logically map to the 0-degree (front) zone
constexpr int NUM_FRONT_SENSORS = 2;
constexpr int FRONT_SENSOR_UP_INDEX   = 0;
constexpr int FRONT_SENSOR_DOWN_INDEX = 1;

constexpr int TOTAL_SENSORS = NUM_BELT_SENSORS + NUM_FRONT_SENSORS; // 12

// --- Motor positions ---
// 8 motors at 45-degree intervals
constexpr int NUM_MOTORS = 8;
constexpr float MOTOR_ANGLES[NUM_MOTORS] = {
    0.0f, 45.0f, 90.0f, 135.0f, 180.0f, 225.0f, 270.0f, 315.0f
};

// --- Vibration level range ---
constexpr uint8_t MAX_MOTOR_LEVEL = 7;  // 3-bit max
constexpr uint8_t MIN_MOTOR_LEVEL = 0;
constexpr int MOTOR_BITS = 3;

// --- Distance thresholds (cm) ---
// Level 7 = closest/most urgent, Level 0 = no detection / too far.
//
// FUTURE: These thresholds should scale based on the user's
// shoulderToFingertipCm dimension from the app's UserDimensions type.
// The scaling factor would be:
//   scaleFactor = userArmLengthCm / REFERENCE_ARM_LENGTH_CM
// Then each threshold is multiplied by scaleFactor, so a user with
// longer arms gets a proportionally larger detection zone.
//
constexpr float REFERENCE_ARM_LENGTH_CM = 65.0f;

// Upper bound for each level (in cm).
// distance >= THRESHOLDS[0] -> level 0 (off)
// distance <  THRESHOLDS[6] -> level 7 (immediate)
constexpr int NUM_THRESHOLDS = 7;
constexpr float DEFAULT_THRESHOLDS_CM[NUM_THRESHOLDS] = {
    100.0f,  // >= 100 cm: level 0 (off)
     85.0f,  // >= 85 cm:  level 1
     70.0f,  // >= 70 cm:  level 2
     55.0f,  // >= 55 cm:  level 3
     40.0f,  // >= 40 cm:  level 4
     25.0f,  // >= 25 cm:  level 5
     10.0f   // >= 10 cm:  level 6;  < 10 cm: level 7
};

// --- Angular spread for motor mapping ---
// Motors within this angle of a sensor get a contribution.
// Motors beyond this angle get nothing.
constexpr float MAX_SPREAD_ANGLE = 45.0f;

// At MAX_SPREAD_ANGLE, the contribution is level / FALLOFF_DIVISOR.
// At 0 degrees, contribution is full level.
// Linear interpolation between.
constexpr float FALLOFF_DIVISOR = 2.0f;

// --- Shift register ---
constexpr int SHIFT_REGISTER_BYTES = 3; // 8 motors * 3 bits = 24 bits = 3 bytes

} // namespace firmware
