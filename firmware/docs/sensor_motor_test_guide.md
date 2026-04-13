# End-to-End Test: Sensor → Pipeline → Motor

## What This Tests

The full chain running automatically in a loop every 50ms:

```
VL53L1X sensor → I2C mux → ESP32 reads distance
    → pipeline categorizes (level 0-7)
    → motor mapper assigns to nearby motors
    → packer encodes 3 bytes
    → shift register drives motor
```

No test blocks, no buttons — the firmware loop in `sensor_task.c` does this continuously once it boots.

---

## Hardware Needed

1. ESP32-S3-DevKitC-1 (the belt MCU)
2. One PCA9548A I2C multiplexer (0x70 or 0x71)
3. One VL53L1X ToF distance sensor
4. Three daisy-chained 74HC595 shift registers
5. One vibration motor + its 3-bit motor driver circuit
6. USB cable for flashing + serial monitor

---

## How the Signal Flows (Sensor → Mux → ESP → Code → Motor)

```
VL53L1X sensor
    ↓ (SDA + SCL wires)
PCA9548A mux (address 0x71), channel 0 output
    ↓ (SDA + SCL wires from mux INPUT side)
ESP32 GPIO 8 (SDA) + GPIO 9 (SCL)
    ↓ (in code, sensor_task.c)
sensor_task says: "sensor index 0 = mux 0x71, channel 0"
    ↓ (pipeline)
sensor index 0 = belt angle 0° → motor_mapper gives full contribution to motor 0
    ↓ (shift register)
motor 0 = chip 1 Q0/Q1/Q2 → motor vibrates
```

The mux is just a switch — it has one I2C input (connected to the ESP32) and 8 I2C outputs (channels 0-7). The sensor plugs into one of those outputs. The code tells the mux "open channel 0" before reading, so the ESP32 talks to the sensor through that channel.

---

## Step 1: Wire the Sensor

The sensor connects to the ESP32 **through** the multiplexer, not directly.

```
ESP32 GPIO 8 (SDA) ──┬── PCA9548A SDA input
ESP32 GPIO 9 (SCL) ──┬── PCA9548A SCL input
                      │
              PCA9548A channel 0 output (SDA + SCL pair)
                      │
                  VL53L1X sensor (SDA, SCL, VIN, GND)
```

| Connection | From | To |
|-----------|------|-----|
| I2C SDA | ESP32 GPIO 8 | PCA9548A SDA input |
| I2C SCL | ESP32 GPIO 9 | PCA9548A SCL input |
| Mux channel SDA | PCA9548A channel 0 SDA output | VL53L1X SDA |
| Mux channel SCL | PCA9548A channel 0 SCL output | VL53L1X SCL |
| Power | 3.3V | PCA9548A VCC + VL53L1X VIN |
| Ground | GND | PCA9548A GND + VL53L1X GND |
| Mux address | PCA9548A A0 pin | GND for 0x70, or VCC for 0x71 |

### Which Mux and Channel?

The firmware's sensor table in `firmware/esp/main/sensor_task.c` defines which sensors it looks for:

```c
static sensor_entry_t s_sensors[NUM_ACTIVE_SENSORS] = {
    { .mux_addr = 0x71, .mux_channel = 0, .sensor_index = 0 },
    { .mux_addr = 0x70, .mux_channel = 1, .sensor_index = 1 },
};
```

For a single-sensor test, use either:
- **MUX1 (0x71), channel 0** → pipeline sensor index 0 (belt sensor at 0°) → **motor 0**
- **MUX0 (0x70), channel 1** → pipeline sensor index 1 (belt sensor at 36°) → **motor 1**

If your mux/channel doesn't match the table, edit the table to match your physical wiring. The other sensor entry will log a warning ("init FAILED") but won't prevent the working sensor from running.

---

## Step 2: Wire the Shift Register Chain to the ESP32

If not already done:

| ESP32 GPIO | 74HC595 Pin | Function |
|-----------|-------------|----------|
| GPIO 2 | SER (pin 14) of chip 1 | Serial data |
| GPIO 35 | SRCLK (pin 11) of ALL chips | Shift clock (tie together) |
| GPIO 38 | RCLK (pin 12) of ALL chips | Latch clock (tie together) |
| GPIO 37 | OE (pin 13) of ALL chips | Output enable — tie together, driven LOW |
| GPIO 36 | SRCLR (pin 10) of ALL chips | Reset — tie together, driven HIGH |

Daisy chain: chip 1 Q7' (pin 9) → chip 2 SER (pin 14) → chip 2 Q7' → chip 3 SER.

Power: VCC (pin 16) to 3.3V or 5V (match motor driver logic), GND (pin 8) to ground.

---

## Step 3: Wire the Motor

The motor connects to 3 consecutive output pins on the 74HC595 chain. Which 3 pins depends on which motor index the sensor maps to.

### Motor 0 (used with sensor index 0)

Motor 0 lives on chip 1 (the nearest chip — the one whose SER connects to ESP32 GPIO 2):

| Pin | Motor 0 bit | Significance |
|-----|-------------|-------------|
| Chip 1, Q0 | Level bit 2 | MSB (value 4) |
| Chip 1, Q1 | Level bit 1 | Middle (value 2) |
| Chip 1, Q2 | Level bit 0 | LSB (value 1) |

Wire these 3 outputs to your motor driver circuit's 3-bit input.

### Full Motor Pin Map (for later when more motors are wired)

| Motor | Belt Angle | Chip | Q pins (MSB → LSB) |
|-------|-----------|------|---------------------|
| 0 | 0° (front) | 1 (nearest) | Q0, Q1, Q2 |
| 1 | 45° | 1 | Q3, Q4, Q5 |
| 2 | 90° | 1 + 2 | Q6, Q7, Q0 |
| 3 | 135° | 2 | Q1, Q2, Q3 |
| 4 | 180° (back) | 2 | Q4, Q5, Q6 |
| 5 | 225° | 2 + 3 | Q7, Q0, Q1 |
| 6 | 270° | 3 | Q2, Q3, Q4 |
| 7 | 315° | 3 (farthest) | Q5, Q6, Q7 |

Note: Motors 2 and 5 straddle chip boundaries — their 3 pins span two physical chips. Electrically this doesn't matter as long as the 3 wires reach the motor driver.

---

## Step 4: Prepare the Code

Make sure all test blocks in `firmware/esp/main/main.c` are commented out (TEST 1 through TEST 4). They should already be wrapped in `/* ... */`. If any are uncommented (especially ones with `while(1)` loops), the firmware will get stuck in the test and never start the sensor task.

---

## Step 5: Flash the Firmware

Open a terminal in `firmware/esp`:

```
C:\esp\v5.5.3\esp-idf\export.bat && idf.py -p COM12 flash monitor
```

Replace `COM12` with your actual COM port.

---

## Step 6: What to Expect in the Serial Monitor

### On Boot

```
BELT_BLE: === Obstacle Detection Belt Firmware ===
SENSOR: Initializing sensors (2 active)...
SENSOR: I2C bus created (SDA=8, SCL=9, 400kHz)
PCA9548A: Multiplexers initialized (MUX0=0x70, MUX1=0x71)
VL53L1X: Sensor device added to bus (addr=0x29)
SENSOR: Sensor 0 (mux 0x71 ch0) → pipeline index 0 — OK
SENSOR: Sensor 1 (mux 0x70 ch1) init FAILED        ← expected if only 1 sensor
SENSOR: 1/2 sensors ready
SHIFT_REG: Shift register initialized (DATA=2, CLK=35, LATCH=38, OE=37, RESET=36)
BELT_BLE: Belt initialized. Scanning for beacons + waiting for phone.
SENSOR: Sensor task started — reading 2 sensors every 50ms
```

### During Operation

Only changes are printed (log stays quiet if nothing moves):

```
SENSOR:   Sensor 0 (mux 0x71 ch0):   45.3 cm
SENSOR:   Motor 0: level 4/7 (was 0)
SENSOR:   Motor 1: level 2/7 (was 0)      ← half contribution from angular spread
```

---

## Step 7: Test It

1. **No obstacle** (nothing within 100cm): Motor off (level 0). No motor log output.
2. **Hand at ~50cm**: Moderate vibration (level 3-4).
3. **Hand at ~20cm**: Strong vibration (level 5-6).
4. **Hand at ~5cm**: Maximum vibration (level 7).
5. **Pull hand away**: Motor ramps down and turns off past 100cm.

### Distance-to-Level Reference

| Distance | Level | Vibration |
|----------|-------|-----------|
| >= 100 cm | 0 | Off |
| 85-99 cm | 1 | Barely perceptible |
| 70-84 cm | 2 | Light |
| 55-69 cm | 3 | Moderate |
| 40-54 cm | 4 | Medium |
| 25-39 cm | 5 | Strong |
| 10-24 cm | 6 | Very strong |
| < 10 cm | 7 | Maximum |

These thresholds scale by the user's arm length (sent from the phone app via BLE command 0x01). The defaults above assume the reference arm length of 65cm.

---

## Troubleshooting

| Symptom | Likely Cause |
|---------|-------------|
| "Sensor 0 init FAILED" at boot | Sensor not wired to the mux channel listed in the sensor table, mux not powered, or wrong mux address (check A0 pin) |
| "Failed to create I2C bus" | GPIO 8/9 conflict or already in use |
| Sensor reads in log but motor doesn't move | Motor driver not wired to the correct 3 shift register pins (Q0/Q1/Q2 of chip 1 for motor 0), or motor power supply disconnected |
| Motor always at max regardless of distance | Only 1 of the 3 bits is connected to the motor — any HIGH = full on. Verify all 3 wires (Q0, Q1, Q2) reach the driver |
| Motor responds but intensity levels seem wrong | MSB/middle/LSB wires swapped — check Q0=MSB, Q1=middle, Q2=LSB |
| Sensor reads "999.0 cm" constantly | Range status != 0 (too noisy, sensor facing dark/distant surface, or sensor not booting properly) |
| Sensor works but motor 0 doesn't respond — a different motor does | Sensor index in the table maps to a different belt angle than expected. Check `.sensor_index` in the sensor table matches motor 0's zone (index 0 = 0°) |
| Boot hangs after "Shift register initialized" | A test block is uncommented with a `while(1)` loop. Comment out all test blocks in main.c |

---

## Changing Which Sensor/Motor Pair to Test

To test a different sensor-motor combination, edit the sensor table in `sensor_task.c`:

```c
static sensor_entry_t s_sensors[NUM_ACTIVE_SENSORS] = {
    { .mux_addr = YOUR_MUX_ADDR,
      .mux_channel = YOUR_CHANNEL,
      .sensor_index = PIPELINE_INDEX,
      .ok = false },
};
```

The `sensor_index` determines which motor responds (via angular mapping):
- Index 0 (0°) → primarily motor 0
- Index 1 (36°) → primarily motor 1
- Index 2 (72°) → primarily motor 2
- ...and so on (index N × 36° → nearest motor at N × 45°)
