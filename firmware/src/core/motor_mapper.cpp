#include "motor_mapper.h"
#include "distance_categorizer.h"
#include <cmath>
#include <algorithm>

namespace firmware {

// Shortest angular distance between two angles on a circle.
// Both in degrees [0, 360). Returns [0, 180].
static float angularDistance(float a, float b) {
    float diff = std::fabs(a - b);
    if (diff > 180.0f) {
        diff = 360.0f - diff;
    }
    return diff;
}

// Compute a motor's contribution from a sensor reading at a given angular distance.
// Returns 0 if beyond MAX_SPREAD_ANGLE.
static uint8_t computeContribution(uint8_t sensorLevel, float angDist) {
    if (sensorLevel == 0) return 0;

    if (angDist <= 1.0f) {
        // Co-located: full strength
        return sensorLevel;
    }

    if (angDist > MAX_SPREAD_ANGLE) {
        // Too far away angularly: no contribution
        return 0;
    }

    // Linear falloff: full at 0 degrees, 1/FALLOFF_DIVISOR at MAX_SPREAD_ANGLE
    float fraction = 1.0f - (1.0f - 1.0f / FALLOFF_DIVISOR) * (angDist / MAX_SPREAD_ANGLE);
    float raw = static_cast<float>(sensorLevel) * fraction;

    // At least 1 if within spread zone (the sensor did detect something nearby)
    int rounded = static_cast<int>(raw + 0.5f);
    return static_cast<uint8_t>(std::max(1, rounded));
}

void mapSensorsToMotors(
    const float sensorDistancesCm[TOTAL_SENSORS],
    uint8_t motorLevels[NUM_MOTORS],
    float armLengthCm
) {
    // Initialize all motors to 0 (off)
    for (int m = 0; m < NUM_MOTORS; m++) {
        motorLevels[m] = 0;
    }

    // Process each belt sensor
    for (int s = 0; s < NUM_BELT_SENSORS; s++) {
        uint8_t level = categorizeDistance(sensorDistancesCm[s], armLengthCm);
        if (level == 0) continue; // Current sensor has no readings

        float sensorAngle = BELT_SENSOR_ANGLES[s];

        for (int m = 0; m < NUM_MOTORS; m++) {
            float angDist = angularDistance(sensorAngle, MOTOR_ANGLES[m]);
            uint8_t contribution = computeContribution(level, angDist);
            // Use max motor value
            if (contribution > motorLevels[m]) {
                motorLevels[m] = contribution;
            }
        }
    }

    // Process front sensors (up and down), both at 0 degrees
    for (int f = 0; f < NUM_FRONT_SENSORS; f++) {
        int sensorIndex = NUM_BELT_SENSORS + f;
        uint8_t level = categorizeDistance(sensorDistancesCm[sensorIndex], armLengthCm);
        if (level == 0) continue;

        for (int m = 0; m < NUM_MOTORS; m++) {
            float angDist = angularDistance(0.0f, MOTOR_ANGLES[m]);
            uint8_t contribution = computeContribution(level, angDist);

            if (contribution > motorLevels[m]) {
                motorLevels[m] = contribution;
            }
        }
    }

    // Clamp all motor levels
    for (int m = 0; m < NUM_MOTORS; m++) {
        if (motorLevels[m] > MAX_MOTOR_LEVEL) {
            motorLevels[m] = MAX_MOTOR_LEVEL;
        }
    }
}

} // namespace firmware
