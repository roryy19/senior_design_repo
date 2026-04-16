# Sensor → Motor Mapping

Definitive mapping from PCB connection-block pins to mux channels,
sensor indices, belt angles, and the motors each sensor activates.
Derived from a full 12-sensor sweep where each sensor was plugged
into a single PCB connector at a time and the resulting log lines
were recorded.

## Mux 0x70 — L0X sensors (5 total, rear arc)

| PCB pair | Mux ch | Index | Angle | Belt position | Motors activated |
|---|---|---|---|---|---|
| **1_cb / 3_cb** | 0x70 ch0 | 3 | 108° | right-rear | 2, 3 |
| **1 / 3** | 0x70 ch1 | 4 | 144° | rear-right | 3, 4 |
| **5 / 7** | 0x70 ch2 | 5 | 180° | rear | 3, 4, 5 |
| **9 / 11** | 0x70 ch3 | 6 | 216° | rear-left | 4, 5 |
| **13 / 15** | 0x70 ch4 | 7 | 252° | left-rear | 5, 6 |

## Mux 0x71 — L1X sensors (7 total, front half + arcs)

| PCB pair | Mux ch | Index | Angle | Belt position | Motors activated |
|---|---|---|---|---|---|
| **17 / 19** | 0x71 ch0 | 0 | 0° | front | 0, 1, 7 |
| **21 / 23** | 0x71 ch1 | 10 | front-up | front (angled up) | 0, 1, 7 |
| **25 / 27** | 0x71 ch2 | 11 | front-down | front (angled down) | 0, 1, 7 |
| **29 / 31** | 0x71 ch3 | 1 | 36° | front-right | 0, 1 |
| **33 / 35** | 0x71 ch4 | 2 | 72° | right | 1, 2 |
| **37 / 39** | 0x71 ch5 | 8 | 288° | left | 6, 7 |
| **41 / 43** | 0x71 ch6 | 9 | 324° | front-left | 0, 7 |

## Sanity checks

- **Count**: 5 L0X + 7 L1X = 12 sensors. Matches 10 belt sensors at
  36° intervals (indices 0-9) plus 2 front sensors (indices 10, 11).
- **Motor coverage**: every sensor activates 2-3 neighboring motors
  within ±45°, confirming `motor_mapper` angular spread behaves as
  designed.
- **Front cluster**: indices 0, 10, 11 all fire the same motor trio
  (0, 1, 7) because they all point at 0°.
- **Belt motor layout reference**: motors at 45° intervals —
  motor 0 = 0° (front), motor 2 = 90° (right), motor 4 = 180° (rear),
  motor 6 = 270° (left).
