#include "distance_categorizer.h"
#include "sensor_config.h"

namespace firmware {

uint8_t categorizeDistance(float distanceCm, float armLengthCm) {
    if (distanceCm <= 0.0f) {
        return MIN_MOTOR_LEVEL;
    }

    // Scale thresholds based on arm length if provided.
    // Longer arm = proportionally larger detection zone.
    float scale = 1.0f;
    if (armLengthCm > 0.0f) {
        scale = armLengthCm / REFERENCE_ARM_LENGTH_CM;
    }

    // Walk thresholds from farthest to nearest.
    for (int i = 0; i < NUM_THRESHOLDS; i++) {
        if (distanceCm >= DEFAULT_THRESHOLDS_CM[i] * scale) {
            return static_cast<uint8_t>(i);
        }
    }

    // Below the last threshold -- maximum urgency
    return MAX_MOTOR_LEVEL;
}

} // namespace firmware
