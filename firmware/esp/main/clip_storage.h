/*
 * clip_storage.h — SPIFFS-based audio clip storage
 *
 * Stores and retrieves audio clips (8-bit unsigned PCM, 8kHz, mono)
 * keyed by beacon MAC address. Each clip is stored as a file in SPIFFS
 * named by the MAC hex string (e.g., "/spiffs/AABBCCDDEEFF.pcm").
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum audio clip size: 16KB (~2 seconds at 8kHz) */
#define MAX_CLIP_SIZE (16 * 1024)

/* Initialize SPIFFS and mount the storage partition. Call once from app_main. */
void clip_storage_init(void);

/*
 * Write an audio clip to flash, keyed by MAC address.
 * mac: 6-byte MAC in little-endian (NimBLE format).
 * data: raw PCM bytes.
 * len: number of bytes.
 * Returns true on success.
 */
bool clip_storage_write(const uint8_t mac[6], const uint8_t *data, size_t len);

/*
 * Read an audio clip from flash.
 * mac: 6-byte MAC in little-endian.
 * buf: output buffer (caller-allocated).
 * max_len: size of buf.
 * Returns the number of bytes read, or 0 if clip not found.
 */
size_t clip_storage_read(const uint8_t mac[6], uint8_t *buf, size_t max_len);

/* Check if a clip exists for the given MAC. */
bool clip_storage_exists(const uint8_t mac[6]);

#ifdef __cplusplus
}
#endif
