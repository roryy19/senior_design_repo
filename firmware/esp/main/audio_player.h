/*
 * audio_player.h — I2S audio playback to MAX98357A (ESP32-S3)
 *
 * Output path: ESP32-S3 I2S0 (standard Philips mode, mono, 16-bit) →
 *   MAX98357A Class-D amp → speaker. No RC filter, no LM386.
 *
 * Clip format in SPIFFS is unchanged: 8-bit unsigned PCM, 8 kHz, mono
 * (silence = 128). The player widens samples to signed 16-bit and
 * upsamples 2x (nearest-neighbor) to 16 kHz on the fly before I2S write.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize the I2S output for the MAX98357A.
 * Call once from app_main.
 *   bclk: serial bit clock pin (BCLK)
 *   lrc:  left/right (word select / LRCLK) pin
 *   din:  data input to DAC (DIN)
 */
void audio_player_init(gpio_num_t bclk, gpio_num_t lrc, gpio_num_t din);

/*
 * Play an audio clip (non-blocking).
 * The caller must keep pcm_data valid until playback finishes
 * (check with audio_player_is_playing()).
 *
 * pcm_data: 8-bit unsigned PCM samples at 8 kHz.
 * length:   number of samples (= number of bytes).
 */
void audio_player_play(const uint8_t *pcm_data, size_t length);

/* Returns true if a clip is currently playing. */
bool audio_player_is_playing(void);

/* Stop playback immediately. */
void audio_player_stop(void);

#ifdef __cplusplus
}
#endif
