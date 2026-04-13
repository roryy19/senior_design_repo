/*
 * shift_register.c — 74HC595 shift register driver implementation
 *
 * Bit-bangs data out to daisy-chained 74HC595 shift registers using
 * 5 GPIO pins. No explicit timing delays are needed between GPIO
 * transitions — the 74HC595 requires ~20ns minimum pulse width, and
 * a single gpio_set_level() call takes ~50-100ns on the ESP32-S3.
 *
 * The 74HC595 captures data on the RISING edge of SRCLK (CLK pin),
 * and transfers the shift register to the output register on the
 * RISING edge of RCLK (LATCH pin).
 */

#include "shift_register.h"

#include <string.h>
#include "esp_log.h"

static const char *TAG = "SHIFT_REG";

void shift_register_init(void)
{
    /* Configure all 5 shift register pins as GPIO outputs */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SR_DATA_PIN)  |
                        (1ULL << SR_CLK_PIN)   |
                        (1ULL << SR_LATCH_PIN) |
                        (1ULL << SR_OE_PIN)    |
                        (1ULL << SR_RESET_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* Set initial pin states:
     *   OE    = LOW  → outputs ENABLED (active low)
     *   RESET = HIGH → normal operation (active low, LOW would clear)
     *   DATA  = LOW  → idle
     *   CLK   = LOW  → idle
     *   LATCH = LOW  → idle
     */
    gpio_set_level(SR_OE_PIN, 0);
    gpio_set_level(SR_RESET_PIN, 1);
    gpio_set_level(SR_DATA_PIN, 0);
    gpio_set_level(SR_CLK_PIN, 0);
    gpio_set_level(SR_LATCH_PIN, 0);

    /* Clear any garbage that might be in the shift registers from
     * power-on. Without this, random motor outputs could be active
     * until the first real data is shifted out. */
    uint8_t zeros[3] = {0, 0, 0};
    shift_register_send(zeros, 3);

    ESP_LOGI(TAG, "Shift register initialized "
             "(DATA=%d, CLK=%d, LATCH=%d, OE=%d, RESET=%d)",
             SR_DATA_PIN, SR_CLK_PIN, SR_LATCH_PIN, SR_OE_PIN, SR_RESET_PIN);
}

void shift_register_send(const uint8_t *data, int num_bytes)
{
    /* Shift out bytes in reverse order, LSB-first within each byte.
     *
     * This reverses the entire 24-bit stream compared to a naive
     * MSB-first send. The reversal is required because the physical
     * motor wiring has motor 0 on the nearest 74HC595 chip (chip 1),
     * while the packer puts motor 0 in the high bits of byte[0]
     * (which would land on the farthest chip with MSB-first).
     *
     * After this reversal:
     *   - Motor 0 (byte[0] bits 7-5) → chip 1 Q0/Q1/Q2 (nearest)
     *   - Motor 7 (byte[2] bits 2-0) → chip 3 Q5/Q6/Q7 (farthest)
     *   - Within each motor: MSB on lower Q#, LSB on higher Q#
     */
    for (int i = num_bytes - 1; i >= 0; i--) {
        for (int bit = 0; bit <= 7; bit++) {
            gpio_set_level(SR_DATA_PIN, (data[i] >> bit) & 0x01);
            gpio_set_level(SR_CLK_PIN, 1);  /* rising edge: bit captured */
            gpio_set_level(SR_CLK_PIN, 0);
        }
    }

    /* Pulse LATCH: rising edge transfers shift register contents
     * to the output register. All motor outputs update at once. */
    gpio_set_level(SR_LATCH_PIN, 1);
    gpio_set_level(SR_LATCH_PIN, 0);
}

void shift_register_clear(void)
{
    uint8_t zeros[3] = {0, 0, 0};
    shift_register_send(zeros, 3);
}
