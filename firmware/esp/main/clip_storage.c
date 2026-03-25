/*
 * clip_storage.c — SPIFFS-based audio clip storage
 *
 * Uses the "storage" SPIFFS partition defined in partitions.csv.
 * Clips are stored as files named by MAC hex string.
 */

#include "clip_storage.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "CLIP_STORE";
static const char *MOUNT_POINT = "/spiffs";

/* Build the file path for a given MAC address */
static void mac_to_path(const uint8_t mac[6], char *path, size_t path_len)
{
    /* MAC stored in little-endian (NimBLE format), display in big-endian */
    snprintf(path, path_len, "%s/%02X%02X%02X%02X%02X%02X.pcm",
             MOUNT_POINT,
             mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
}

void clip_storage_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path       = MOUNT_POINT,
        .partition_label = "storage",
        .max_files       = 5,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info("storage", &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: %u bytes total, %u bytes used",
             (unsigned)total, (unsigned)used);
}

bool clip_storage_write(const uint8_t mac[6], const uint8_t *data, size_t len)
{
    if (len == 0 || len > MAX_CLIP_SIZE) {
        ESP_LOGW(TAG, "Invalid clip size: %u bytes (max %u)", (unsigned)len, MAX_CLIP_SIZE);
        return false;
    }

    char path[64];
    mac_to_path(mac, path, sizeof(path));

    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", path);
        return false;
    }

    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    if (written != len) {
        ESP_LOGE(TAG, "Write incomplete: %u of %u bytes", (unsigned)written, (unsigned)len);
        return false;
    }

    ESP_LOGI(TAG, "Stored clip: %s (%u bytes, %.1fs)",
             path, (unsigned)len, (float)len / 8000.0f);
    return true;
}

size_t clip_storage_read(const uint8_t mac[6], uint8_t *buf, size_t max_len)
{
    char path[64];
    mac_to_path(mac, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGD(TAG, "Clip not found: %s", path);
        return 0;
    }

    size_t bytes_read = fread(buf, 1, max_len, f);
    fclose(f);

    ESP_LOGI(TAG, "Read clip: %s (%u bytes)", path, (unsigned)bytes_read);
    return bytes_read;
}

bool clip_storage_exists(const uint8_t mac[6])
{
    char path[64];
    mac_to_path(mac, path, sizeof(path));

    struct stat st;
    return (stat(path, &st) == 0);
}
