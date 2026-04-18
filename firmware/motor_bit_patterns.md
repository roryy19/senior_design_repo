# Motor Bit Patterns — Shift Register Reference

Bit layout for 8 motors (3 bits each = 24 bits = 3 bytes) sent to the
74HC595 shift register via `shift_register_send(bytes, 3)`.

## Bit layout

24-bit stream, MSB first. Motor 0 shifts out first.

| Motor | 24-bit range | Byte location           |
|-------|--------------|-------------------------|
| 0     | bits 23-21   | byte[0] bits 7-5        |
| 1     | bits 20-18   | byte[0] bits 4-2        |
| 2     | bits 17-15   | byte[0] bits 1-0 + byte[1] bit 7 (straddle) |
| 3     | bits 14-12   | byte[1] bits 6-4        |
| 4     | bits 11-9    | byte[1] bits 3-1        |
| 5     | bits 8-6     | byte[1] bit 0 + byte[2] bits 7-6 (straddle) |
| 6     | bits 5-3     | byte[2] bits 5-3        |
| 7     | bits 2-0     | byte[2] bits 2-0        |

## Single-motor pattern tables

Each table shows the 3-byte value to drive **only that motor** at level 1-7.
All other motors are off.

### Motor 0 — byte[0] bits 7-5

| Level | byte[0] | byte[1] | byte[2] |
|-------|---------|---------|---------|
| 1     | 0x20    | 0x00    | 0x00    |
| 2     | 0x40    | 0x00    | 0x00    |
| 3     | 0x60    | 0x00    | 0x00    |
| 4     | 0x80    | 0x00    | 0x00    |
| 5     | 0xA0    | 0x00    | 0x00    |
| 6     | 0xC0    | 0x00    | 0x00    |
| 7     | 0xE0    | 0x00    | 0x00    |

### Motor 1 — byte[0] bits 4-2

| Level | byte[0] | byte[1] | byte[2] |
|-------|---------|---------|---------|
| 1     | 0x04    | 0x00    | 0x00    |
| 2     | 0x08    | 0x00    | 0x00    |
| 3     | 0x0C    | 0x00    | 0x00    |
| 4     | 0x10    | 0x00    | 0x00    |
| 5     | 0x14    | 0x00    | 0x00    |
| 6     | 0x18    | 0x00    | 0x00    |
| 7     | 0x1C    | 0x00    | 0x00    |

### Motor 2 — byte[0] bits 1-0 + byte[1] bit 7 (straddle)

| Level | byte[0] | byte[1] | byte[2] |
|-------|---------|---------|---------|
| 1     | 0x00    | 0x80    | 0x00    |
| 2     | 0x01    | 0x00    | 0x00    |
| 3     | 0x01    | 0x80    | 0x00    |
| 4     | 0x02    | 0x00    | 0x00    |
| 5     | 0x02    | 0x80    | 0x00    |
| 6     | 0x03    | 0x00    | 0x00    |
| 7     | 0x03    | 0x80    | 0x00    |

### Motor 3 — byte[1] bits 6-4

| Level | byte[0] | byte[1] | byte[2] |
|-------|---------|---------|---------|
| 1     | 0x00    | 0x10    | 0x00    |
| 2     | 0x00    | 0x20    | 0x00    |
| 3     | 0x00    | 0x30    | 0x00    |
| 4     | 0x00    | 0x40    | 0x00    |
| 5     | 0x00    | 0x50    | 0x00    |
| 6     | 0x00    | 0x60    | 0x00    |
| 7     | 0x00    | 0x70    | 0x00    |

### Motor 4 — byte[1] bits 3-1

| Level | byte[0] | byte[1] | byte[2] |
|-------|---------|---------|---------|
| 1     | 0x00    | 0x02    | 0x00    |
| 2     | 0x00    | 0x04    | 0x00    |
| 3     | 0x00    | 0x06    | 0x00    |
| 4     | 0x00    | 0x08    | 0x00    |
| 5     | 0x00    | 0x0A    | 0x00    |
| 6     | 0x00    | 0x0C    | 0x00    |
| 7     | 0x00    | 0x0E    | 0x00    |

### Motor 5 — byte[1] bit 0 + byte[2] bits 7-6 (straddle)

| Level | byte[0] | byte[1] | byte[2] |
|-------|---------|---------|---------|
| 1     | 0x00    | 0x00    | 0x40    |
| 2     | 0x00    | 0x00    | 0x80    |
| 3     | 0x00    | 0x00    | 0xC0    |
| 4     | 0x00    | 0x01    | 0x00    |
| 5     | 0x00    | 0x01    | 0x40    |
| 6     | 0x00    | 0x01    | 0x80    |
| 7     | 0x00    | 0x01    | 0xC0    |

### Motor 6 — byte[2] bits 5-3

| Level | byte[0] | byte[1] | byte[2] |
|-------|---------|---------|---------|
| 1     | 0x00    | 0x00    | 0x08    |
| 2     | 0x00    | 0x00    | 0x10    |
| 3     | 0x00    | 0x00    | 0x18    |
| 4     | 0x00    | 0x00    | 0x20    |
| 5     | 0x00    | 0x00    | 0x28    |
| 6     | 0x00    | 0x00    | 0x30    |
| 7     | 0x00    | 0x00    | 0x38    |

### Motor 7 — byte[2] bits 2-0

| Level | byte[0] | byte[1] | byte[2] |
|-------|---------|---------|---------|
| 1     | 0x00    | 0x00    | 0x01    |
| 2     | 0x00    | 0x00    | 0x02    |
| 3     | 0x00    | 0x00    | 0x03    |
| 4     | 0x00    | 0x00    | 0x04    |
| 5     | 0x00    | 0x00    | 0x05    |
| 6     | 0x00    | 0x00    | 0x06    |
| 7     | 0x00    | 0x00    | 0x07    |

## All-motors-same-level patterns (TEST 4.2)

Level N repeated 8 times across the 24-bit stream.

| Level | byte[0] | byte[1] | byte[2] |
|-------|---------|---------|---------|
| 1     | 0x24    | 0x92    | 0x49    |
| 2     | 0x49    | 0x24    | 0x92    |
| 3     | 0x6D    | 0xB6    | 0xDB    |
| 4     | 0x92    | 0x49    | 0x24    |
| 5     | 0xB6    | 0xDB    | 0x6D    |
| 6     | 0xDB    | 0x6D    | 0xB6    |
| 7     | 0xFF    | 0xFF    | 0xFF    |

## Usage template

Copy one of the pattern tables above into a test block like:

```c
const uint8_t patterns[7][3] = {
    {0x??, 0x??, 0x??}, // level 1
    {0x??, 0x??, 0x??}, // level 2
    {0x??, 0x??, 0x??}, // level 3
    {0x??, 0x??, 0x??}, // level 4
    {0x??, 0x??, 0x??}, // level 5
    {0x??, 0x??, 0x??}, // level 6
    {0x??, 0x??, 0x??}, // level 7
};
for (int lvl = 0; lvl < 7; lvl++) {
    ESP_LOGI(TAG, "motor N at level %d", lvl + 1);
    shift_register_send(patterns[lvl], 3);
    vTaskDelay(pdMS_TO_TICKS(1000));
}
shift_register_clear();
```

## How to derive a pattern for any (motor, level)

1. `value = level << (21 - motor * 3)` — 24-bit value
2. `byte[0] = (value >> 16) & 0xFF`
3. `byte[1] = (value >> 8) & 0xFF`
4. `byte[2] = value & 0xFF`

Example — motor 5 at level 3: `value = 3 << 6 = 0xC0` → `{0x00, 0x00, 0xC0}`.
