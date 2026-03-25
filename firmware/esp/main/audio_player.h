/*
 * audio_player.h — PWM-based audio playback for ESP32-S3
 *
 * The ESP32-S3 has no built-in DAC. Audio is output via LEDC PWM on a
 * single GPIO pin. A simple RC low-pass filter (10kOhm + 0.1uF) converts
 * the PWM to analog for the LM386 amplifier.
 *
 * Audio format: 8-bit unsigned PCM, 8kHz, mono.
 * Silence = 128 (midpoint of 0-255 range).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize the PWM audio output on the specified GPIO pin.
 * Call once from app_main.
 */
void audio_player_init(gpio_num_t gpio);

/*
 * Play an audio clip (non-blocking).
 * The data is NOT copied — the caller must keep the buffer valid until
 * playback completes (check with audio_player_is_playing()).
 *
 * pcm_data: 8-bit unsigned PCM samples at 8kHz.
 * length:   number of samples (= number of bytes).
 */
void audio_player_play(const uint8_t *pcm_data, size_t length);

/* Returns true if a clip is currently playing. */
bool audio_player_is_playing(void);

/* Stop playback immediately and return output to silence. */
void audio_player_stop(void);

#ifdef __cplusplus
}
#endif
