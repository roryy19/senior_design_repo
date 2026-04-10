/*
 * shift_register.h — 74HC595 shift register driver for motor output
 *
 * Drives daisy-chained 74HC595 shift registers to control 8 vibration
 * motors. Each motor gets a 3-bit intensity level (0-7), packed into
 * 3 bytes (24 bits) by the shift_register_packer in src/core/.
 *
 * GPIO pin assignments (directly wired to 74HC595):
 *   DATA  (GPIO 2)  → SER / DS    — serial data input
 *   CLK   (GPIO 36) → SRCLK / SHCP — shift register clock
 *   LATCH (GPIO 37) → RCLK / STCP  — storage register clock (output latch)
 *   OE    (GPIO 38) → OE           — output enable (active LOW)
 *   RESET (GPIO 35) → SRCLR / MR   — shift register clear (active LOW)
 *
 * Data flow:
 *   1. Bit-bang 24 bits via DATA + CLK (MSB first)
 *   2. Pulse LATCH to transfer shift register → output register
 *   3. All 8 motor outputs update simultaneously on the latch pulse
 *
 * NOTE on bit ordering: This driver shifts MSB-first, meaning the first
 * bit shifted out (motor 0, bit 23) ends up at the far end of the
 * daisy chain. If during testing motor 0 doesn't respond but motor 7
 * does, the physical wiring is reversed — flip to LSB-first in
 * shift_register_send() by changing the inner loop direction.
 */

#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- GPIO pin assignments for shift register --- */
#define SR_DATA_PIN     GPIO_NUM_2
#define SR_CLK_PIN      GPIO_NUM_35
#define SR_LATCH_PIN    GPIO_NUM_38
#define SR_OE_PIN       GPIO_NUM_37
#define SR_RESET_PIN    GPIO_NUM_36

/*
 * Initialize all shift register GPIO pins and clear the outputs.
 *
 * Configures DATA, CLK, LATCH, OE, RESET as GPIO outputs.
 * Sets OE=LOW (outputs enabled) and RESET=HIGH (normal operation).
 * Sends 3 zero bytes to clear any garbage from power-on.
 *
 * Call once from app_main(), before sensor_task_start().
 */
void shift_register_init(void);

/*
 * Shift out raw bytes to the 74HC595 chain and latch.
 *
 * Sends num_bytes bytes MSB-first via DATA/CLK, then pulses LATCH
 * to update all outputs simultaneously.
 *
 * For motor control, pass the 3-byte output from packMotorLevels():
 *   shift_register_send(packed_bytes, 3);
 *
 * @param data      Pointer to byte array to shift out
 * @param num_bytes Number of bytes to send (typically 3)
 */
void shift_register_send(const uint8_t *data, int num_bytes);

/*
 * Clear all shift register outputs (all motors off).
 * Convenience function — sends 3 zero bytes.
 */
void shift_register_clear(void);

#ifdef __cplusplus
}
#endif
