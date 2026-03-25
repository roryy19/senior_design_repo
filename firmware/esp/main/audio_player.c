/*
 * audio_player.c — PWM-based audio playback for ESP32-S3
 *
 * Uses the LEDC peripheral to output a high-frequency PWM signal whose
 * duty cycle represents the audio sample value. An esp_timer callback
 * runs at 8kHz to advance through the PCM buffer.
 *
 * Hardware connection:
 *   ESP32 GPIO -> 10kOhm resistor -> LM386 input
 *                                 |
 *                            0.1uF cap
 *                                 |
 *                                GND
 */

#include "audio_player.h"

#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "AUDIO";

/* PWM configuration */
#define PWM_FREQ_HZ       62500           /* ~62.5kHz — inaudible carrier */
#define PWM_SPEED_MODE    LEDC_LOW_SPEED_MODE
#define PWM_TIMER         LEDC_TIMER_0
#define PWM_CHANNEL       LEDC_CHANNEL_0
#define PWM_DUTY_SILENCE  128             /* Midpoint = silence for unsigned 8-bit */

/* Sample rate timer */
#define SAMPLE_PERIOD_US  125             /* 1,000,000 / 8000 Hz = 125 us */

/* Playback state */
static const uint8_t *current_pcm = NULL;
static size_t current_len = 0;
static size_t current_pos = 0;
static volatile bool playing = false;

static esp_timer_handle_t sample_timer = NULL;

/* ---------- Timer callback: runs at 8kHz ---------- */

static void IRAM_ATTR sample_timer_cb(void *arg)
{
    if (current_pos < current_len) {
        ledc_set_duty(PWM_SPEED_MODE, PWM_CHANNEL, current_pcm[current_pos]);
        ledc_update_duty(PWM_SPEED_MODE, PWM_CHANNEL);
        current_pos++;
    } else {
        /* Clip finished — stop timer, return to silence */
        esp_timer_stop(sample_timer);
        ledc_set_duty(PWM_SPEED_MODE, PWM_CHANNEL, PWM_DUTY_SILENCE);
        ledc_update_duty(PWM_SPEED_MODE, PWM_CHANNEL);
        playing = false;
    }
}

/* ---------- Public API ---------- */

void audio_player_init(gpio_num_t gpio)
{
    /* Configure LEDC timer */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = PWM_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,   /* 256 levels = matches 8-bit PCM */
        .timer_num       = PWM_TIMER,
        .freq_hz         = PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    /* Configure LEDC channel */
    ledc_channel_config_t chan_cfg = {
        .gpio_num   = gpio,
        .speed_mode = PWM_SPEED_MODE,
        .channel    = PWM_CHANNEL,
        .timer_sel  = PWM_TIMER,
        .duty       = PWM_DUTY_SILENCE,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan_cfg));

    /* Create the sample-rate timer (created once, started/stopped per clip) */
    esp_timer_create_args_t timer_args = {
        .callback        = sample_timer_cb,
        .name            = "audio_sample",
        .dispatch_method = ESP_TIMER_TASK,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &sample_timer));

    ESP_LOGI(TAG, "Audio player initialized (GPIO %d, PWM %d Hz)",
             gpio, PWM_FREQ_HZ);
}

void audio_player_play(const uint8_t *pcm_data, size_t length)
{
    /* Stop any current playback first */
    if (playing) {
        audio_player_stop();
    }

    if (length == 0 || pcm_data == NULL) return;

    current_pcm = pcm_data;
    current_len = length;
    current_pos = 0;
    playing = true;

    ESP_LOGI(TAG, "Playing clip (%u bytes, %.1fs)",
             (unsigned)length, (float)length / 8000.0f);

    /* Start the 8kHz sample timer */
    esp_timer_start_periodic(sample_timer, SAMPLE_PERIOD_US);
}

bool audio_player_is_playing(void)
{
    return playing;
}

void audio_player_stop(void)
{
    if (sample_timer) {
        esp_timer_stop(sample_timer);
    }

    ledc_set_duty(PWM_SPEED_MODE, PWM_CHANNEL, PWM_DUTY_SILENCE);
    ledc_update_duty(PWM_SPEED_MODE, PWM_CHANNEL);

    playing = false;
}
