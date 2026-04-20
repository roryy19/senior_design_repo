#include "test_helpers.h"

// Define the shared counters
int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;

// Forward declarations from each test file
void runDistanceCategorizerTests();
void runMotorMapperTests();
void runShiftRegisterPackerTests();
void runPipelineTests();
void runShiftRegisterOutputTests();
void runEspDriverSimTests();

int main() {
    printf("=== Firmware Core Logic Test Suite ===\n\n");

    printf("--- Distance Categorizer ---\n");
    runDistanceCategorizerTests();

    printf("\n--- Motor Mapper ---\n");
    runMotorMapperTests();

    printf("\n--- Shift Register Packer ---\n");
    runShiftRegisterPackerTests();

    printf("\n--- Pipeline ---\n");
    runPipelineTests();

    printf("\n--- Shift Register Output Simulation ---\n");
    runShiftRegisterOutputTests();

    printf("\n--- ESP Driver Bit-Exact Simulation ---\n");
    runEspDriverSimTests();

    printSummary();

    return tests_failed > 0 ? 1 : 0;
}
