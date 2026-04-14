/*
 * vl53l0x.c — VL53L0X driver implementation for ESP-IDF
 *
 * How this compares to the VL53L1X driver:
 *
 *   VL53L1X (vl53l1x.c)              VL53L0X (this file)
 *   ──────────────────────────────    ──────────────────────────────
 *   16-bit register addresses         8-bit register addresses
 *   Write: [reg_hi, reg_lo, data]     Write: [reg, data]
 *   Model ID 0xEA at reg 0x010F       Model ID 0xEE at reg 0xC0
 *   91-byte config blob               ~80 tuning register writes
 *   Continuous mode: reg 0x0087       Continuous mode: reg 0x00
 *   Data ready: reg 0x0031 bit 0      Data ready: reg 0x13 bits[2:0]
 *   Distance: reg 0x0096 (16-bit)     Distance: reg 0x1E (16-bit)
 *   Clear int: reg 0x0086             Clear int: reg 0x0B
 *
 * Init sequence adapted from:
 *   - Pololu VL53L0X Arduino library (MIT license)
 *   - ST VL53L0X API (STSW-IMG005, BSD-3 license)
 *
 * The init involves many "magic" register writes that configure the
 * sensor's internal firmware. These values come from ST's reference
 * implementation and are required for correct operation.
 */

#include "vl53l0x.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "VL53L0X";

#define I2C_TIMEOUT_MS  100

/* ── VL53L0X register addresses (8-bit, unlike VL53L1X's 16-bit) ── */
#define REG_SYSRANGE_START                      0x00
#define REG_SYSTEM_SEQUENCE_CONFIG              0x01
#define REG_SYSTEM_INTERRUPT_CONFIG_GPIO        0x0A
#define REG_SYSTEM_INTERRUPT_CLEAR              0x0B
#define REG_RESULT_INTERRUPT_STATUS             0x13
#define REG_RESULT_RANGE_STATUS                 0x14
#define REG_FINAL_RANGE_CONFIG_MIN_COUNT_RATE   0x44
#define REG_GPIO_HV_MUX_ACTIVE_HIGH            0x84
#define REG_VHV_CONFIG_PAD_SCL_SDA_EXTSUP_HV   0x89
#define REG_IDENTIFICATION_MODEL_ID             0xC0
#define REG_GLOBAL_CONFIG_SPAD_ENABLES_REF_0    0xB0

/* ── Module state ────────────────────────────────────────────────── */
static i2c_master_dev_handle_t s_dev_handle;
static uint8_t s_stop_variable = 0;  /* Read during init, used by start_ranging */

/* ═══════════════════════════════════════════════════════════════════ *
 *                    Low-level I2C register operations                *
 *                                                                    *
 * The VL53L0X uses 8-bit register addresses. Each I2C transaction    *
 * starts with 1 address byte (vs 2 for VL53L1X).                    *
 * ═══════════════════════════════════════════════════════════════════ */

static vl53l0x_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    esp_err_t rc = i2c_master_transmit(s_dev_handle, buf, 2, I2C_TIMEOUT_MS);
    return (rc == ESP_OK) ? VL53L0X_OK : VL53L0X_ERROR;
}

static vl53l0x_err_t write_reg16(uint8_t reg, uint16_t value)
{
    uint8_t buf[3] = { reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
    esp_err_t rc = i2c_master_transmit(s_dev_handle, buf, 3, I2C_TIMEOUT_MS);
    return (rc == ESP_OK) ? VL53L0X_OK : VL53L0X_ERROR;
}

static vl53l0x_err_t write_multi(uint8_t reg, const uint8_t *data, int len)
{
    /* Max multi-write is 6 bytes (SPAD enable map) + 1 reg byte */
    uint8_t buf[7];
    if (len > 6) return VL53L0X_ERROR;
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    esp_err_t rc = i2c_master_transmit(s_dev_handle, buf, 1 + len, I2C_TIMEOUT_MS);
    return (rc == ESP_OK) ? VL53L0X_OK : VL53L0X_ERROR;
}

static vl53l0x_err_t read_reg(uint8_t reg, uint8_t *value)
{
    esp_err_t rc = i2c_master_transmit_receive(s_dev_handle, &reg, 1,
                                                value, 1, I2C_TIMEOUT_MS);
    return (rc == ESP_OK) ? VL53L0X_OK : VL53L0X_ERROR;
}

static vl53l0x_err_t read_reg16(uint8_t reg, uint16_t *value)
{
    uint8_t data[2];
    esp_err_t rc = i2c_master_transmit_receive(s_dev_handle, &reg, 1,
                                                data, 2, I2C_TIMEOUT_MS);
    if (rc != ESP_OK) return VL53L0X_ERROR;
    *value = ((uint16_t)data[0] << 8) | data[1];  /* big-endian on wire */
    return VL53L0X_OK;
}

static vl53l0x_err_t read_multi(uint8_t reg, uint8_t *data, int len)
{
    esp_err_t rc = i2c_master_transmit_receive(s_dev_handle, &reg, 1,
                                                data, len, I2C_TIMEOUT_MS);
    return (rc == ESP_OK) ? VL53L0X_OK : VL53L0X_ERROR;
}

/* ═══════════════════════════════════════════════════════════════════ *
 *                     Sensor init helper functions                    *
 * ═══════════════════════════════════════════════════════════════════ */

/* Load tuning settings — a long block of "magic" register writes from
 * ST's API. These configure the sensor's internal firmware parameters.
 * Without these, the sensor won't produce valid measurements.
 *
 * Source: Pololu VL53L0X library / ST STSW-IMG005.
 * Each line writes one register. The register addresses and values
 * are undocumented internal parameters determined by ST. */
static vl53l0x_err_t load_tuning_settings(void)
{
    vl53l0x_err_t e;

    e = write_reg(0xFF, 0x01); if (e) return e;
    e = write_reg(0x00, 0x00); if (e) return e;
    e = write_reg(0xFF, 0x00); if (e) return e;
    e = write_reg(0x09, 0x00); if (e) return e;
    e = write_reg(0x10, 0x00); if (e) return e;
    e = write_reg(0x11, 0x00); if (e) return e;
    e = write_reg(0x24, 0x01); if (e) return e;
    e = write_reg(0x25, 0xFF); if (e) return e;
    e = write_reg(0x75, 0x00); if (e) return e;

    e = write_reg(0xFF, 0x01); if (e) return e;
    e = write_reg(0x4E, 0x2C); if (e) return e;
    e = write_reg(0x48, 0x00); if (e) return e;
    e = write_reg(0x30, 0x20); if (e) return e;

    e = write_reg(0xFF, 0x00); if (e) return e;
    e = write_reg(0x30, 0x09); if (e) return e;
    e = write_reg(0x54, 0x00); if (e) return e;
    e = write_reg(0x31, 0x04); if (e) return e;
    e = write_reg(0x32, 0x03); if (e) return e;
    e = write_reg(0x40, 0x83); if (e) return e;
    e = write_reg(0x46, 0x25); if (e) return e;
    e = write_reg(0x60, 0x00); if (e) return e;
    e = write_reg(0x27, 0x00); if (e) return e;
    e = write_reg(0x50, 0x06); if (e) return e;
    e = write_reg(0x51, 0x00); if (e) return e;
    e = write_reg(0x52, 0x96); if (e) return e;
    e = write_reg(0x56, 0x08); if (e) return e;
    e = write_reg(0x57, 0x30); if (e) return e;
    e = write_reg(0x61, 0x00); if (e) return e;
    e = write_reg(0x62, 0x00); if (e) return e;
    e = write_reg(0x64, 0x00); if (e) return e;
    e = write_reg(0x65, 0x00); if (e) return e;
    e = write_reg(0x66, 0xA0); if (e) return e;

    e = write_reg(0xFF, 0x01); if (e) return e;
    e = write_reg(0x22, 0x32); if (e) return e;
    e = write_reg(0x47, 0x14); if (e) return e;
    e = write_reg(0x49, 0xFF); if (e) return e;
    e = write_reg(0x4A, 0x00); if (e) return e;

    e = write_reg(0xFF, 0x00); if (e) return e;
    e = write_reg(0x7A, 0x0A); if (e) return e;
    e = write_reg(0x7B, 0x00); if (e) return e;
    e = write_reg(0x78, 0x21); if (e) return e;

    e = write_reg(0xFF, 0x01); if (e) return e;
    e = write_reg(0x23, 0x34); if (e) return e;
    e = write_reg(0x42, 0x00); if (e) return e;
    e = write_reg(0x44, 0xFF); if (e) return e;
    e = write_reg(0x45, 0x26); if (e) return e;
    e = write_reg(0x46, 0x05); if (e) return e;
    e = write_reg(0x40, 0x40); if (e) return e;
    e = write_reg(0x0E, 0x06); if (e) return e;
    e = write_reg(0x20, 0x1A); if (e) return e;
    e = write_reg(0x43, 0x40); if (e) return e;

    e = write_reg(0xFF, 0x00); if (e) return e;
    e = write_reg(0x34, 0x03); if (e) return e;
    e = write_reg(0x35, 0x44); if (e) return e;

    e = write_reg(0xFF, 0x01); if (e) return e;
    e = write_reg(0x31, 0x04); if (e) return e;
    e = write_reg(0x4B, 0x09); if (e) return e;
    e = write_reg(0x4C, 0x05); if (e) return e;
    e = write_reg(0x4D, 0x04); if (e) return e;

    e = write_reg(0xFF, 0x00); if (e) return e;
    e = write_reg(0x44, 0x00); if (e) return e;
    e = write_reg(0x45, 0x20); if (e) return e;
    e = write_reg(0x47, 0x08); if (e) return e;
    e = write_reg(0x48, 0x28); if (e) return e;
    e = write_reg(0x67, 0x00); if (e) return e;
    e = write_reg(0x70, 0x04); if (e) return e;
    e = write_reg(0x71, 0x01); if (e) return e;
    e = write_reg(0x72, 0xFE); if (e) return e;
    e = write_reg(0x76, 0x00); if (e) return e;
    e = write_reg(0x77, 0x00); if (e) return e;

    e = write_reg(0xFF, 0x01); if (e) return e;
    e = write_reg(0x0D, 0x01); if (e) return e;

    e = write_reg(0xFF, 0x00); if (e) return e;
    e = write_reg(0x80, 0x01); if (e) return e;
    e = write_reg(0x01, 0xF8); if (e) return e;

    e = write_reg(0xFF, 0x01); if (e) return e;
    e = write_reg(0x8E, 0x01); if (e) return e;
    e = write_reg(0x00, 0x01); if (e) return e;

    e = write_reg(0xFF, 0x00); if (e) return e;
    e = write_reg(0x80, 0x00); if (e) return e;

    return VL53L0X_OK;
}

/* Read SPAD info from the sensor (count and type).
 * This is needed to configure the reference SPADs correctly.
 * Sequence from Pololu VL53L0X library / ST API. */
static vl53l0x_err_t get_spad_info(uint8_t *count, bool *is_aperture)
{
    vl53l0x_err_t e;
    uint8_t tmp;

    e = write_reg(0x80, 0x01); if (e) return e;
    e = write_reg(0xFF, 0x01); if (e) return e;
    e = write_reg(0x00, 0x00); if (e) return e;
    e = write_reg(0xFF, 0x06); if (e) return e;

    e = read_reg(0x83, &tmp); if (e) return e;
    e = write_reg(0x83, tmp | 0x04); if (e) return e;

    e = write_reg(0xFF, 0x07); if (e) return e;
    e = write_reg(0x81, 0x01); if (e) return e;
    e = write_reg(0x80, 0x01); if (e) return e;
    e = write_reg(0x94, 0x6B); if (e) return e;
    e = write_reg(0x83, 0x00); if (e) return e;

    /* Wait for register 0x83 to become non-zero */
    for (int i = 0; i < 200; i++) {
        e = read_reg(0x83, &tmp);
        if (e) return e;
        if (tmp != 0x00) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (tmp == 0x00) return VL53L0X_TIMEOUT;

    e = write_reg(0x83, 0x01); if (e) return e;
    e = read_reg(0x92, &tmp);  if (e) return e;

    *count = tmp & 0x7F;
    *is_aperture = (tmp >> 7) & 0x01;

    e = write_reg(0x81, 0x00); if (e) return e;
    e = write_reg(0xFF, 0x06); if (e) return e;

    e = read_reg(0x83, &tmp); if (e) return e;
    e = write_reg(0x83, tmp & ~0x04); if (e) return e;

    e = write_reg(0xFF, 0x01); if (e) return e;
    e = write_reg(0x00, 0x01); if (e) return e;
    e = write_reg(0xFF, 0x00); if (e) return e;
    e = write_reg(0x80, 0x00); if (e) return e;

    return VL53L0X_OK;
}

/* Configure reference SPADs based on the SPAD info read earlier.
 * Reads the current SPAD enable map, adjusts it, and writes it back.
 * From Pololu VL53L0X library / ST API. */
static vl53l0x_err_t set_ref_spads(uint8_t spad_count, bool is_aperture)
{
    vl53l0x_err_t e;
    uint8_t spad_map[6];

    e = read_multi(REG_GLOBAL_CONFIG_SPAD_ENABLES_REF_0, spad_map, 6);
    if (e) return e;

    /* First aperture SPAD is at index 12 in the 48-SPAD array */
    uint8_t first_spad = is_aperture ? 12 : 0;
    uint8_t enabled = 0;

    for (uint8_t i = 0; i < 48; i++) {
        if (i < first_spad || enabled == spad_count) {
            /* Disable this SPAD */
            spad_map[i / 8] &= ~(1 << (i % 8));
        } else if ((spad_map[i / 8] >> (i % 8)) & 0x01) {
            /* Already enabled — count it */
            enabled++;
        }
    }

    return write_multi(REG_GLOBAL_CONFIG_SPAD_ENABLES_REF_0, spad_map, 6);
}

/* Perform a single reference calibration measurement.
 * Used for both VHV calibration (vhv_init_byte=0x40) and
 * phase calibration (vhv_init_byte=0x00). */
static vl53l0x_err_t perform_single_ref_cal(uint8_t vhv_init_byte)
{
    vl53l0x_err_t e;

    e = write_reg(REG_SYSRANGE_START, 0x01 | vhv_init_byte);
    if (e) return e;

    /* Wait for measurement to complete */
    uint8_t status = 0;
    for (int i = 0; i < 200; i++) {
        e = read_reg(REG_RESULT_INTERRUPT_STATUS, &status);
        if (e) return e;
        if (status & 0x07) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (!(status & 0x07)) return VL53L0X_TIMEOUT;

    e = write_reg(REG_SYSTEM_INTERRUPT_CLEAR, 0x01); if (e) return e;
    e = write_reg(REG_SYSRANGE_START, 0x00);
    return e;
}

/* ═══════════════════════════════════════════════════════════════════ *
 *                         Public API functions                       *
 * ═══════════════════════════════════════════════════════════════════ */

void vl53l0x_set_device(i2c_master_dev_handle_t dev)
{
    s_dev_handle = dev;
}

vl53l0x_err_t vl53l0x_sensor_init(void)
{
    vl53l0x_err_t e;

    /* ── Verify sensor by reading model ID ───────────────────────── */
    uint8_t model_id = 0;
    e = read_reg(REG_IDENTIFICATION_MODEL_ID, &model_id);
    if (e != VL53L0X_OK || model_id != 0xEE) {
        ESP_LOGE(TAG, "Sensor not found! Expected model ID 0xEE, got 0x%02X",
                 model_id);
        return VL53L0X_ERROR;
    }
    ESP_LOGI(TAG, "Sensor detected (model ID: 0x%02X)", model_id);

    /* ── Data init ───────────────────────────────────────────────── */

    /* Enable 2V8 mode (internal voltage regulator for 2.8V I/O) */
    uint8_t vhv_cfg;
    e = read_reg(REG_VHV_CONFIG_PAD_SCL_SDA_EXTSUP_HV, &vhv_cfg);
    if (e) return e;
    e = write_reg(REG_VHV_CONFIG_PAD_SCL_SDA_EXTSUP_HV, vhv_cfg | 0x01);
    if (e) return e;

    /* Set I2C standard mode */
    e = write_reg(0x88, 0x00); if (e) return e;

    /* Read the "stop variable" — an internal value needed by the
     * start/stop continuous ranging sequence. */
    e = write_reg(0x80, 0x01); if (e) return e;
    e = write_reg(0xFF, 0x01); if (e) return e;
    e = write_reg(0x00, 0x00); if (e) return e;
    e = read_reg(0x91, &s_stop_variable); if (e) return e;
    e = write_reg(0x00, 0x01); if (e) return e;
    e = write_reg(0xFF, 0x00); if (e) return e;
    e = write_reg(0x80, 0x00); if (e) return e;

    /* Enable all measurement sequence steps */
    e = write_reg(REG_SYSTEM_SEQUENCE_CONFIG, 0xFF); if (e) return e;

    /* ── Static init: SPAD configuration ─────────────────────────── */
    uint8_t spad_count;
    bool spad_is_aperture;
    e = get_spad_info(&spad_count, &spad_is_aperture);
    if (e != VL53L0X_OK) {
        ESP_LOGW(TAG, "SPAD info read failed, continuing with defaults");
    } else {
        e = set_ref_spads(spad_count, spad_is_aperture);
        if (e != VL53L0X_OK) {
            ESP_LOGW(TAG, "SPAD config failed, continuing with defaults");
        }
    }

    /* ── Load tuning settings (large block of ST magic values) ──── */
    e = load_tuning_settings();
    if (e != VL53L0X_OK) {
        ESP_LOGE(TAG, "Failed to load tuning settings");
        return e;
    }

    /* ── GPIO interrupt config: new sample ready ─────────────────── */
    e = write_reg(REG_SYSTEM_INTERRUPT_CONFIG_GPIO, 0x04); if (e) return e;

    /* Set interrupt polarity to active low */
    uint8_t gpio_hv;
    e = read_reg(REG_GPIO_HV_MUX_ACTIVE_HIGH, &gpio_hv);
    if (e) return e;
    e = write_reg(REG_GPIO_HV_MUX_ACTIVE_HIGH, gpio_hv & ~0x10);
    if (e) return e;

    e = write_reg(REG_SYSTEM_INTERRUPT_CLEAR, 0x01); if (e) return e;

    /* ── Set default signal rate limit (0.25 MCPS in 9.7 fixed-point) */
    e = write_reg16(REG_FINAL_RANGE_CONFIG_MIN_COUNT_RATE, 0x0020);
    if (e) return e;

    /* ── Reference calibration ───────────────────────────────────── */

    /* VHV calibration */
    e = write_reg(REG_SYSTEM_SEQUENCE_CONFIG, 0x01); if (e) return e;
    e = perform_single_ref_cal(0x40);
    if (e != VL53L0X_OK) {
        ESP_LOGW(TAG, "VHV calibration failed");
    }

    /* Phase calibration */
    e = write_reg(REG_SYSTEM_SEQUENCE_CONFIG, 0x02); if (e) return e;
    e = perform_single_ref_cal(0x00);
    if (e != VL53L0X_OK) {
        ESP_LOGW(TAG, "Phase calibration failed");
    }

    /* Restore full sequence config */
    e = write_reg(REG_SYSTEM_SEQUENCE_CONFIG, 0xE8); if (e) return e;

    ESP_LOGI(TAG, "Sensor configured: default timing, continuous mode ready");
    return VL53L0X_OK;
}

vl53l0x_err_t vl53l0x_start_ranging(void)
{
    vl53l0x_err_t e;

    /* Start continuous back-to-back ranging.
     * The preamble sequence using s_stop_variable is required by ST's API
     * to properly start the sensor's measurement engine. */
    e = write_reg(0x80, 0x01); if (e) return e;
    e = write_reg(0xFF, 0x01); if (e) return e;
    e = write_reg(0x00, 0x00); if (e) return e;
    e = write_reg(0x91, s_stop_variable); if (e) return e;
    e = write_reg(0x00, 0x01); if (e) return e;
    e = write_reg(0xFF, 0x00); if (e) return e;
    e = write_reg(0x80, 0x00); if (e) return e;

    /* 0x02 = continuous back-to-back mode (measures as fast as possible,
     * ~33ms per measurement with default timing budget) */
    return write_reg(REG_SYSRANGE_START, 0x02);
}

vl53l0x_err_t vl53l0x_data_ready(bool *ready)
{
    *ready = false;
    uint8_t status = 0;
    vl53l0x_err_t e = read_reg(REG_RESULT_INTERRUPT_STATUS, &status);
    if (e != VL53L0X_OK) return e;
    /* Bits [2:0] indicate the interrupt type. Non-zero = data ready. */
    *ready = (status & 0x07) != 0;
    return VL53L0X_OK;
}

vl53l0x_err_t vl53l0x_read_distance(uint16_t *distance_mm)
{
    /* Distance is at RESULT_RANGE_STATUS + 10 = register 0x1E (2 bytes).
     * This is the final range result in millimeters. */
    return read_reg16(REG_RESULT_RANGE_STATUS + 10, distance_mm);
}

vl53l0x_err_t vl53l0x_read_range_status(uint8_t *status)
{
    uint8_t raw = 0;
    vl53l0x_err_t e = read_reg(REG_RESULT_RANGE_STATUS, &raw);
    if (e != VL53L0X_OK) return e;

    /* Raw DeviceRangeStatus is in bits [6:3] of register 0x14.
     *
     * IMPORTANT: Unlike the VL53L1X where raw 9 → "valid" (0),
     * the VL53L0X uses raw 11 → "valid". We must decode this
     * so that sensor_task can check (status == 0) for both types.
     *
     * Raw values from ST VL53L0X API:
     *   0  = no measurement         → 255
     *   1  = sigma fail             → 1
     *   2  = signal fail            → 2
     *   3  = min range fail         → 3
     *   4  = phase fail             → 4
     *   5  = hardware fail          → 5
     *   6  = range valid (no wrap)  → 0  (valid)
     *   7  = wrap target fail       → 7
     *   8  = no target              → 255
     *   11 = range valid (normal)   → 0  (valid)
     */
    uint8_t raw_status = (raw >> 3) & 0x0F;

    switch (raw_status) {
        case 11: *status = 0; break;   /* Range valid (normal case) */
        case 6:  *status = 0; break;   /* Range valid (no wrap check) */
        case 1:  *status = 1; break;   /* Sigma fail */
        case 2:  *status = 2; break;   /* Signal fail */
        case 3:  *status = 3; break;   /* Min range fail */
        case 4:  *status = 4; break;   /* Phase fail */
        case 5:  *status = 5; break;   /* Hardware fail */
        case 7:  *status = 7; break;   /* Wrap target fail */
        default: *status = 255; break; /* No measurement / unknown */
    }
    return VL53L0X_OK;
}

vl53l0x_err_t vl53l0x_clear_interrupt(void)
{
    return write_reg(REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
}
