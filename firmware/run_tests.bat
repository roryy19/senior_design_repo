@echo off
echo Compiling firmware tests...
g++ -std=c++17 -Wall -Wextra -I src/core -o tests/run_tests.exe ^
    src/core/distance_categorizer.cpp ^
    src/core/motor_mapper.cpp ^
    src/core/shift_register_packer.cpp ^
    src/core/pipeline.cpp ^
    src/core/shift_register_output.cpp ^
    tests/test_main.cpp ^
    tests/test_distance_categorizer.cpp ^
    tests/test_motor_mapper.cpp ^
    tests/test_shift_register_packer.cpp ^
    tests/test_pipeline.cpp ^
    tests/test_shift_register_output.cpp ^
    tests/test_esp_driver_sim.cpp

if %ERRORLEVEL% NEQ 0 (
    echo Compilation failed.
    exit /b 1
)

echo.
tests\run_tests.exe
