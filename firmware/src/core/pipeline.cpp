#include "pipeline.h"
#include "motor_mapper.h"
#include "shift_register_packer.h"
#include <cstring>

namespace firmware {

void processSensorReadings(
    const float sensorDistancesCm[TOTAL_SENSORS],
    uint8_t shiftRegisterOut[SHIFT_REGISTER_BYTES],
    uint8_t motorLevelsOut[NUM_MOTORS],
    float armLengthCm
) {
    uint8_t levels[NUM_MOTORS];
    mapSensorsToMotors(sensorDistancesCm, levels, armLengthCm);
    packMotorLevels(levels, shiftRegisterOut);

    if (motorLevelsOut != nullptr) {
        std::memcpy(motorLevelsOut, levels, NUM_MOTORS);
    }
}

} // namespace firmware
