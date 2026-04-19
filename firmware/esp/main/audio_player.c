/*
 * audio_player.c — I2S audio playback to MAX98357A (ESP32-S3)
 *
 * Hardware:
 *   ESP32-S3 I2S0 master TX →
 *     BCLK → MAX98357A BCLK
 *     LRC  → MAX98357A LRC (word select)
 *     DIN  → MAX98357A DIN
 *   MAX98357A output → speaker. No MCLK needed.
 *
 * Source clips are 8 kHz 8-bit unsigned PCM in SPIFFS. This module
 * widens them to signed 16-bit and upsamples 2x (nearest-neighbor)
 * to 16 kHz while feeding the I2S channel. A dedicated FreeRTOS
 * task drains clips; audio_player_play() signals it via semaphore.
 */

#include "audio_player.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

static const char *TAG = "AUDIO";

/* I2S output format */
#define I2S_SAMPLE_RATE_HZ   16000      /* 2x the 8kHz source; simple upsample */
#define CHUNK_SAMPLES_IN     128        /* 8-bit samples read per iteration */
#define CHUNK_SAMPLES_OUT    (CHUNK_SAMPLES_IN * 2)  /* 16-bit, 2x upsample */

/* Playback state (set before giving semaphore) */
static const uint8_t   *s_pcm = NULL;
static size_t           s_len = 0;
static volatile size_t  s_pos = 0;
static volatile bool    s_playing = false;
static volatile bool    s_stop_req = false;

static SemaphoreHandle_t s_start_sem = NULL;
static i2s_chan_handle_t s_tx_chan = NULL;

/* ---------- I2S writer task ---------- */

static void audio_task(void *arg)
{
    int16_t out_buf[CHUNK_SAMPLES_OUT];

    for (;;) {
        /* Wait until a new clip is loaded */
        xSemaphoreTake(s_start_sem, portMAX_DELAY);

        while (s_pos < s_len && !s_stop_req) {
            /* Pull up to CHUNK_SAMPLES_IN bytes from the source clip */
            size_t take = s_len - s_pos;
            if (take > CHUNK_SAMPLES_IN) take = CHUNK_SAMPLES_IN;

            /* Widen + upsample 2x: each 8-bit unsigned sample → two signed 16-bit */
            for (size_t i = 0; i < take; i++) {
                int16_t v = ((int16_t)s_pcm[s_pos + i] - 128) << 8;
                out_buf[2 * i]     = v;
                out_buf[2 * i + 1] = v;
            }
            s_pos += take;

            /* Blocking write at the sample rate (16 kHz) */
            size_t bytes_written = 0;
            i2s_channel_write(s_tx_chan, out_buf, take * 2 * sizeof(int16_t),
                              &bytes_written, portMAX_DELAY);
        }

        /* Tail silence: one chunk of zeros to flush DMA and drop the line */
        memset(out_buf, 0, sizeof(out_buf));
        size_t bytes_written = 0;
        i2s_channel_write(s_tx_chan, out_buf, sizeof(out_buf),
                          &bytes_written, portMAX_DELAY);

        s_playing = false;
        s_stop_req = false;
        ESP_LOGI(TAG, "Clip finished");
    }
}

/* ---------- Public API ---------- */

void audio_player_init(gpio_num_t bclk, gpio_num_t lrc, gpio_num_t din)
{
    /* Create TX channel on I2S0, master role */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL));

    /* Standard Philips mode, 16-bit mono at 16 kHz */
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = bclk,
            .ws   = lrc,
            .dout = din,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = 0, .bclk_inv = 0, .ws_inv = 0 },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_chan));

    /* Semaphore for play() → task */
    s_start_sem = xSemaphoreCreateBinary();

    /* Playback task (priority 6: higher than sensor=5, below NimBLE host) */
    xTaskCreate(audio_task, "audio", 3072, NULL, 6, NULL);

    ESP_LOGI(TAG, "Audio player initialized (I2S: BCLK=%d LRC=%d DIN=%d, %d Hz 16-bit)",
             bclk, lrc, din, I2S_SAMPLE_RATE_HZ);
}

void audio_player_play(const uint8_t *pcm_data, size_t length)
{
    if (length == 0 || pcm_data == NULL) return;

    /* If a clip is currently playing, ask it to stop first */
    if (s_playing) {
        s_stop_req = true;
        /* Brief wait for the task to return to its top (no strict guarantee;
         * worst case the new clip queues after the old finishes its chunk) */
        for (int i = 0; i < 20 && s_playing; i++) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    s_pcm = pcm_data;
    s_len = length;
    s_pos = 0;
    s_stop_req = false;
    s_playing = true;

    ESP_LOGI(TAG, "Playing clip (%u bytes, %.1fs)",
             (unsigned)length, (float)length / 8000.0f);

    xSemaphoreGive(s_start_sem);
}

bool audio_player_is_playing(void)
{
    return s_playing;
}

void audio_player_stop(void)
{
    if (s_playing) s_stop_req = true;
}
