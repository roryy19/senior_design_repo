/*
 * vl53l1x.c — VL53L1X driver implementation for ESP-IDF
 *
 * How this replaces the Arduino code:
 *
 *   ARDUINO                         THIS FILE (ESP-IDF)
 *   ───────────────────────────────  ──────────────────────────────────
 *   Wire.begin(8, 9)                i2c_new_master_bus() — sets up the
 *                                   I2C hardware on GPIO 8 (SDA) / 9 (SCL)
 *
 *   Wire.setClock(400000)           .scl_speed_hz = 400000 in device config
 *
 *   Wire.beginTransmission(0x29)    i2c_master_transmit() — sends bytes
 *   Wire.write(data)                over I2C to the sensor at address 0x29
 *   Wire.endTransmission()
 *
 *   Wire.requestFrom(0x29, N)       i2c_master_transmit_receive() — sends
 *   Wire.read()                     the register address, then reads back
 *
 *   sensor.init()                   vl53l1x_init() — writes the 91-byte
 *                                   default config blob to the sensor
 *
 *   sensor.read()                   vl53l1x_read_distance() — reads the
 *                                   16-bit distance register
 *
 * The VL53L1X is special: it uses 16-bit register addresses (2 bytes),
 * not the typical 8-bit addresses most I2C sensors use. Every I2C
 * transaction sends [reg_addr_high, reg_addr_low] before the data.
 *
 * The default configuration blob is from ST's VL53L1X Ultra Lite Driver
 * (STSW-IMG009), which is published under BSD-3 license.
 */

#include "vl53l1x.h"

#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "VL53L1X";

/* ── Sensor I2C address (default, same as Arduino code) ───────────── */
#define VL53L1X_ADDR            0x29

/* ── I2C bus pins (matching teammate's Arduino wiring) ────────────── */
#define I2C_SDA_PIN             8
#define I2C_SCL_PIN             9
#define I2C_FREQ_HZ             400000  /* 400 kHz Fast Mode */
#define I2C_TIMEOUT_MS          100

/* ── VL53L1X register addresses ───────────────────────────────────── */
/* These are the sensor's internal register addresses, documented in
 * ST's Ultra Lite Driver. The sensor has hundreds of registers; we
 * only need these few for basic ranging. */
#define REG_FIRMWARE_STATUS                     0x00E5
#define REG_MODEL_ID                            0x010F
#define REG_VHV_CONFIG_TIMEOUT_MACROP_LOOP_BOUND 0x0008
#define REG_MYSTERY_0B                          0x000B  /* Undocumented, required by ST ULD */
#define REG_GPIO_HV_MUX_CTRL                    0x0030
#define REG_GPIO_TIO_HV_STATUS                  0x0031
#define REG_RANGE_CONFIG_TIMEOUT_A              0x005E
#define REG_RANGE_CONFIG_VCSEL_PERIOD_A         0x0060
#define REG_RANGE_CONFIG_TIMEOUT_B              0x0061
#define REG_RANGE_CONFIG_VCSEL_PERIOD_B         0x0063
#define REG_RANGE_CONFIG_VALID_PHASE_HIGH       0x0069
#define REG_INTERMEASUREMENT_PERIOD             0x006C
#define REG_SD_CONFIG_WOI                       0x0078
#define REG_SD_CONFIG_INITIAL_PHASE             0x007A
#define REG_SYSTEM_INTERRUPT_CLEAR              0x0086
#define REG_SYSTEM_START                        0x0087
#define REG_RESULT_RANGE_STATUS                 0x0089
#define REG_RESULT_DISTANCE                     0x0096
#define REG_RESULT_OSC_CALIBRATE                0x00DE
#define REG_PHASECAL_CONFIG_TIMEOUT             0x004B

/* ── I2C handles (module-level, created once during init) ─────────── */
static i2c_master_bus_handle_t s_bus_handle;
static i2c_master_dev_handle_t s_dev_handle;
static bool s_initialized = false;

/* ── Default configuration blob from ST's Ultra Lite Driver ───────── *
 * This 91-byte array is written to consecutive registers 0x002D–0x0087
 * during sensor init. It contains ST's default tuning parameters for
 * the sensor's firmware. Without this, the sensor won't produce valid
 * measurements. Each byte's target register is shown in the comment.
 *
 * Source: ST STSW-IMG009, VL53L1X_api.c, BSD-3 license.             */
static const uint8_t DEFAULT_CONFIG[] = {
    0x00, /* 0x2D : set bit 2 and 5 to 1 for fast plus mode (1MHz I2C), else don't touch */
    0x00, /* 0x2E : bit 0 if I2C pulled up at 1.8V, else set bit 0 to 1 (pull up at AVDD) */
    0x00, /* 0x2F : bit 0 if GPIO pulled up at 1.8V, else set bit 0 to 1 (pull up at AVDD) */
    0x01, /* 0x30 : set bit 4 to 0 for active high interrupt and target 1 to 1 for ready */
    0x02, /* 0x31 : bit 1 = interrupt polarity active low */
    0x00, /* 0x32 : not user-modifiable */
    0x02, /* 0x33 : not user-modifiable */
    0x08, /* 0x34 : not user-modifiable */
    0x00, /* 0x35 : not user-modifiable */
    0x08, /* 0x36 : not user-modifiable */
    0x10, /* 0x37 : not user-modifiable */
    0x01, /* 0x38 : not user-modifiable */
    0x01, /* 0x39 : not user-modifiable */
    0x00, /* 0x3A : not user-modifiable */
    0x00, /* 0x3B : not user-modifiable */
    0x00, /* 0x3C : not user-modifiable */
    0x00, /* 0x3D : not user-modifiable */
    0xFF, /* 0x3E : not user-modifiable */
    0x00, /* 0x3F : not user-modifiable */
    0x0F, /* 0x40 : not user-modifiable */
    0x00, /* 0x41 : not user-modifiable */
    0x00, /* 0x42 : not user-modifiable */
    0x00, /* 0x43 : not user-modifiable */
    0x00, /* 0x44 : not user-modifiable */
    0x00, /* 0x45 : not user-modifiable */
    0x20, /* 0x46 : interrupt configuration 0x20 = new sample ready */
    0x0B, /* 0x47 : not user-modifiable */
    0x00, /* 0x48 : not user-modifiable */
    0x00, /* 0x49 : not user-modifiable */
    0x02, /* 0x4A : not user-modifiable */
    0x0A, /* 0x4B : not user-modifiable */
    0x21, /* 0x4C : not user-modifiable */
    0x00, /* 0x4D : not user-modifiable */
    0x00, /* 0x4E : not user-modifiable */
    0x05, /* 0x4F : not user-modifiable */
    0x00, /* 0x50 : not user-modifiable */
    0x00, /* 0x51 : not user-modifiable */
    0x00, /* 0x52 : not user-modifiable */
    0x00, /* 0x53 : not user-modifiable */
    0xC8, /* 0x54 : not user-modifiable */
    0x00, /* 0x55 : not user-modifiable */
    0x00, /* 0x56 : not user-modifiable */
    0x38, /* 0x57 : not user-modifiable */
    0xFF, /* 0x58 : not user-modifiable */
    0x01, /* 0x59 : not user-modifiable */
    0x00, /* 0x5A : not user-modifiable */
    0x08, /* 0x5B : not user-modifiable */
    0x00, /* 0x5C : not user-modifiable */
    0x00, /* 0x5D : not user-modifiable */
    0x01, /* 0x5E : not user-modifiable */
    0xCC, /* 0x5F : not user-modifiable */
    0x0F, /* 0x60 : not user-modifiable */
    0x01, /* 0x61 : not user-modifiable */
    0xF1, /* 0x62 : not user-modifiable */
    0x0D, /* 0x63 : not user-modifiable */
    0x01, /* 0x64 : Sigma threshold MSB (14.2 format) default 90 mm */
    0x68, /* 0x65 : Sigma threshold LSB */
    0x00, /* 0x66 : Min count Rate MSB default 128 Mcps */
    0x80, /* 0x67 : Min count Rate LSB */
    0x08, /* 0x68 : not user-modifiable */
    0xB8, /* 0x69 : not user-modifiable */
    0x00, /* 0x6A : not user-modifiable */
    0x00, /* 0x6B : not user-modifiable */
    0x00, /* 0x6C : Intermeasurement period MSB (32-bit register) */
    0x00, /* 0x6D : Intermeasurement period */
    0x0F, /* 0x6E : Intermeasurement period */
    0x89, /* 0x6F : Intermeasurement period LSB */
    0x00, /* 0x70 : not user-modifiable */
    0x00, /* 0x71 : not user-modifiable */
    0x00, /* 0x72 : distance threshold high MSB (mm) */
    0x00, /* 0x73 : distance threshold high LSB */
    0x00, /* 0x74 : distance threshold low MSB */
    0x00, /* 0x75 : distance threshold low LSB */
    0x00, /* 0x76 : not user-modifiable */
    0x01, /* 0x77 : not user-modifiable */
    0x0F, /* 0x78 : not user-modifiable */
    0x0D, /* 0x79 : not user-modifiable */
    0x0E, /* 0x7A : not user-modifiable */
    0x0E, /* 0x7B : not user-modifiable */
    0x00, /* 0x7C : not user-modifiable */
    0x00, /* 0x7D : not user-modifiable */
    0x02, /* 0x7E : not user-modifiable */
    0xC7, /* 0x7F : ROI center, default 199 */
    0xFF, /* 0x80 : XY ROI (X=16, Y=16) */
    0x9B, /* 0x81 : not user-modifiable */
    0x00, /* 0x82 : not user-modifiable */
    0x00, /* 0x83 : not user-modifiable */
    0x00, /* 0x84 : not user-modifiable */
    0x01, /* 0x85 : not user-modifiable */
    0x00, /* 0x86 : clear interrupt, 0x01 = clear */
    0x00, /* 0x87 : start ranging, 0x40 = start, 0x00 = stop */
};

#define DEFAULT_CONFIG_START_REG  0x002D
#define DEFAULT_CONFIG_SIZE       sizeof(DEFAULT_CONFIG) /* 91 bytes */

/* ═══════════════════════════════════════════════════════════════════ *
 *                  Low-level I2C register operations                 *
 *                                                                    *
 * The VL53L1X uses 16-bit register addresses. This is unusual —      *
 * most I2C sensors use 8-bit addresses. It means every I2C           *
 * transaction starts with 2 address bytes (high byte first).         *
 *                                                                    *
 *   Write: [reg_hi, reg_lo, data...]   (one I2C transaction)        *
 *   Read:  [reg_hi, reg_lo] → [data...]  (write addr, then read)   *
 * ═══════════════════════════════════════════════════════════════════ */

static vl53l1x_err_t write_byte(uint16_t reg, uint8_t value)
{
    uint8_t buf[3] = {
        (uint8_t)(reg >> 8),    /* register address high byte */
        (uint8_t)(reg & 0xFF),  /* register address low byte */
        value                   /* data byte */
    };
    esp_err_t rc = i2c_master_transmit(s_dev_handle, buf, 3, I2C_TIMEOUT_MS);
    return (rc == ESP_OK) ? VL53L1X_OK : VL53L1X_ERROR;
}

static vl53l1x_err_t write_word(uint16_t reg, uint16_t value)
{
    uint8_t buf[4] = {
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFF),
        (uint8_t)(value >> 8),    /* data high byte (big-endian on wire) */
        (uint8_t)(value & 0xFF),  /* data low byte */
    };
    esp_err_t rc = i2c_master_transmit(s_dev_handle, buf, 4, I2C_TIMEOUT_MS);
    return (rc == ESP_OK) ? VL53L1X_OK : VL53L1X_ERROR;
}

static vl53l1x_err_t write_dword(uint16_t reg, uint32_t value)
{
    uint8_t buf[6] = {
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFF),
        (uint8_t)((value >> 24) & 0xFF),
        (uint8_t)((value >> 16) & 0xFF),
        (uint8_t)((value >>  8) & 0xFF),
        (uint8_t)((value >>  0) & 0xFF),
    };
    esp_err_t rc = i2c_master_transmit(s_dev_handle, buf, 6, I2C_TIMEOUT_MS);
    return (rc == ESP_OK) ? VL53L1X_OK : VL53L1X_ERROR;
}

static vl53l1x_err_t read_byte(uint16_t reg, uint8_t *value)
{
    /* Send the 2-byte register address, then read 1 byte back.
     * i2c_master_transmit_receive does a write-then-read in one
     * transaction (with I2C repeated start), which is what the
     * sensor expects. */
    uint8_t reg_buf[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    esp_err_t rc = i2c_master_transmit_receive(s_dev_handle, reg_buf, 2,
                                                value, 1, I2C_TIMEOUT_MS);
    return (rc == ESP_OK) ? VL53L1X_OK : VL53L1X_ERROR;
}

static vl53l1x_err_t read_word(uint16_t reg, uint16_t *value)
{
    uint8_t reg_buf[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    uint8_t data[2];
    esp_err_t rc = i2c_master_transmit_receive(s_dev_handle, reg_buf, 2,
                                                data, 2, I2C_TIMEOUT_MS);
    if (rc != ESP_OK) return VL53L1X_ERROR;
    *value = ((uint16_t)data[0] << 8) | data[1]; /* big-endian on wire */
    return VL53L1X_OK;
}

/* ═══════════════════════════════════════════════════════════════════ *
 *                     Sensor init helper functions                   *
 * ═══════════════════════════════════════════════════════════════════ */

/* Wait for the sensor's firmware to finish booting after power-on.
 * The VL53L1X takes up to ~1.2 seconds to boot. We poll a status
 * register until the firmware reports ready. */
static vl53l1x_err_t wait_for_boot(void)
{
    for (int i = 0; i < 150; i++) {  /* 150 × 10ms = 1.5s max wait */
        uint8_t state = 0;
        vl53l1x_err_t err = read_byte(REG_FIRMWARE_STATUS, &state);
        if (err != VL53L1X_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (state != 0) {
            return VL53L1X_OK;  /* Firmware booted */
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGE(TAG, "Sensor firmware did not boot within 1.5s");
    return VL53L1X_TIMEOUT;
}

/* Write the 91-byte default configuration blob.
 * This is equivalent to VL53L1X_SensorInit() in ST's ULD.
 * Written byte-by-byte to consecutive registers 0x002D–0x0087. */
static vl53l1x_err_t write_default_config(void)
{
    for (int i = 0; i < (int)DEFAULT_CONFIG_SIZE; i++) {
        uint16_t reg = DEFAULT_CONFIG_START_REG + i;
        vl53l1x_err_t err = write_byte(reg, DEFAULT_CONFIG[i]);
        if (err != VL53L1X_OK) {
            ESP_LOGE(TAG, "Failed writing config at register 0x%04X", reg);
            return err;
        }
    }
    return VL53L1X_OK;
}

/* Set distance mode to Long (up to 4m range).
 * This writes 6 registers that control the VCSEL (laser) pulse timing.
 * Short mode would use different values for better close-range accuracy,
 * but we use Long to match the teammate's Arduino code. */
static vl53l1x_err_t set_distance_mode_long(void)
{
    vl53l1x_err_t err;
    err = write_byte(REG_PHASECAL_CONFIG_TIMEOUT, 0x0A);  if (err) return err;
    err = write_byte(REG_RANGE_CONFIG_VCSEL_PERIOD_A, 0x0F);  if (err) return err;
    err = write_byte(REG_RANGE_CONFIG_VCSEL_PERIOD_B, 0x0D);  if (err) return err;
    err = write_byte(REG_RANGE_CONFIG_VALID_PHASE_HIGH, 0x78); if (err) return err;
    err = write_word(REG_SD_CONFIG_WOI, 0x0F0D);              if (err) return err;
    err = write_word(REG_SD_CONFIG_INITIAL_PHASE, 0x0E0E);    if (err) return err;
    return VL53L1X_OK;
}

/* Set timing budget to 50ms (Long mode).
 * The timing budget controls how long the sensor spends on each
 * measurement. Longer = more accurate but slower. 50ms is a good
 * balance, matching the teammate's Arduino code (TIMING_BUDGET_MS = 50).
 *
 * The register values come from a lookup table in ST's ULD — they're
 * pre-calculated based on internal timing constants. */
static vl53l1x_err_t set_timing_budget_50ms(void)
{
    vl53l1x_err_t err;
    /* These values are for Long mode + 50ms, from ST's ULD lookup table */
    err = write_word(REG_RANGE_CONFIG_TIMEOUT_A, 0x00AD);  if (err) return err;
    err = write_word(REG_RANGE_CONFIG_TIMEOUT_B, 0x00C6);  if (err) return err;
    return VL53L1X_OK;
}

/* Set inter-measurement period to 55ms.
 * This is the time between the start of consecutive measurements.
 * Must be >= timing budget. We use budget + 5ms as ST recommends.
 *
 * The formula from ST's ULD reads an oscillator calibration value
 * from the sensor, then computes: period_reg = osc_cal * period_ms * 1.075 */
static vl53l1x_err_t set_inter_measurement_55ms(void)
{
    uint16_t osc_cal = 0;
    vl53l1x_err_t err = read_word(REG_RESULT_OSC_CALIBRATE, &osc_cal);
    if (err != VL53L1X_OK) return err;
    osc_cal &= 0x3FF;

    /* If osc_cal reads as 0 (sensor not calibrated), use a safe default */
    if (osc_cal == 0) osc_cal = 0x64;

    uint32_t period_reg = (uint32_t)((float)osc_cal * 55.0f * 1.075f);
    return write_dword(REG_INTERMEASUREMENT_PERIOD, period_reg);
}

/* ═══════════════════════════════════════════════════════════════════ *
 *                         Public API functions                       *
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Create device on an external bus (for mux configurations) ───── */
vl53l1x_err_t vl53l1x_create_on_bus(i2c_master_bus_handle_t bus)
{
    s_bus_handle = bus;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = VL53L1X_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    esp_err_t rc = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &s_dev_handle);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(rc));
        return VL53L1X_ERROR;
    }

    ESP_LOGI(TAG, "Sensor device added to bus (addr=0x%02X)", VL53L1X_ADDR);
    return VL53L1X_OK;
}

/* ── Run sensor init sequence (on currently-selected mux channel) ── */
vl53l1x_err_t vl53l1x_sensor_init(void)
{
    /* Verify the sensor is connected by reading model ID */
    uint8_t model_id = 0;
    vl53l1x_err_t err = read_byte(REG_MODEL_ID, &model_id);
    if (err != VL53L1X_OK || model_id != 0xEA) {
        ESP_LOGE(TAG, "Sensor not found! Expected model ID 0xEA, got 0x%02X. "
                 "Check wiring: VIN->3.3V, GND->GND, SDA->GPIO%d, SCL->GPIO%d",
                 model_id, I2C_SDA_PIN, I2C_SCL_PIN);
        return VL53L1X_ERROR;
    }
    ESP_LOGI(TAG, "Sensor detected (model ID: 0x%02X)", model_id);

    /* Wait for firmware boot (up to ~1.2s after power-on) */
    err = wait_for_boot();
    if (err != VL53L1X_OK) return err;
    ESP_LOGI(TAG, "Sensor firmware ready");

    /* Write the 91-byte default configuration blob */
    err = write_default_config();
    if (err != VL53L1X_OK) return err;

    /* Perform one dummy measurement (required by ST for calibration) */
    err = write_byte(REG_SYSTEM_START, 0x40);
    if (err != VL53L1X_OK) return err;

    bool ready = false;
    for (int i = 0; i < 100 && !ready; i++) {
        vl53l1x_data_ready(&ready);
        if (!ready) vTaskDelay(pdMS_TO_TICKS(10));
    }
    vl53l1x_clear_interrupt();
    write_byte(REG_SYSTEM_START, 0x00);

    /* Post-init register tweaks (from ST ULD) */
    write_byte(REG_VHV_CONFIG_TIMEOUT_MACROP_LOOP_BOUND, 0x09);
    write_byte(REG_MYSTERY_0B, 0x00);

    /* Configure for Long mode + 50ms timing budget */
    err = set_distance_mode_long();     if (err != VL53L1X_OK) return err;
    err = set_timing_budget_50ms();     if (err != VL53L1X_OK) return err;
    err = set_inter_measurement_55ms(); if (err != VL53L1X_OK) return err;

    s_initialized = true;
    ESP_LOGI(TAG, "Sensor configured: Long mode, 50ms timing budget");
    return VL53L1X_OK;
}

/* ── Single-sensor convenience (creates bus + device + inits) ────── */
vl53l1x_err_t vl53l1x_init(void)
{
    /* Step 1: Create the I2C bus (ESP-IDF equivalent of Wire.begin(8, 9)) */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t rc = i2c_new_master_bus(&bus_cfg, &s_bus_handle);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(rc));
        return VL53L1X_ERROR;
    }

    ESP_LOGI(TAG, "I2C bus initialized (SDA=%d, SCL=%d, %dkHz)",
             I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ / 1000);

    /* Step 2: Add device + run sensor init */
    vl53l1x_err_t err = vl53l1x_create_on_bus(s_bus_handle);
    if (err != VL53L1X_OK) return err;

    return vl53l1x_sensor_init();
}

vl53l1x_err_t vl53l1x_start_ranging(void)
{
    if (!s_initialized) return VL53L1X_ERROR;
    /* Write 0x40 to the start register to begin continuous ranging.
     * The sensor will now take a measurement every ~55ms (our inter-
     * measurement period) and set the data-ready flag when done. */
    return write_byte(REG_SYSTEM_START, 0x40);
}

vl53l1x_err_t vl53l1x_data_ready(bool *ready)
{
    *ready = false;
    uint8_t status = 0;
    vl53l1x_err_t err = read_byte(REG_GPIO_TIO_HV_STATUS, &status);
    if (err != VL53L1X_OK) return err;
    /* With default config (register 0x0030 = 0x01, bit 4 = 0),
     * the interrupt polarity is active high. Data ready when bit 0 = 1. */
    *ready = (status & 0x01) != 0;
    return VL53L1X_OK;
}

vl53l1x_err_t vl53l1x_read_distance(uint16_t *distance_mm)
{
    return read_word(REG_RESULT_DISTANCE, distance_mm);
}

vl53l1x_err_t vl53l1x_read_range_status(uint8_t *status)
{
    uint8_t raw = 0;
    vl53l1x_err_t err = read_byte(REG_RESULT_RANGE_STATUS, &raw);
    if (err != VL53L1X_OK) return err;

    /* The raw register value must be mapped through ST's lookup table
     * to get the user-facing status code. The raw hardware codes don't
     * match the documented status values directly.
     *
     * Lookup table from ST's ULD (VL53L1X_api.c):
     *   Raw  9 → 0 (Valid)         Raw  6 → 1 (Sigma fail)
     *   Raw  4 → 2 (Signal fail)   Raw  8 → 3 (Min range / wrap)
     *   Raw  5 → 4 (Phase fail)    Raw  3 → 5 (Hardware fail)
     *   Raw  7 → 7 (Wrapped)       Others → 255 (Unknown)
     */
    static const uint8_t status_lookup[24] = {
        255, 255, 255, 5, 2, 4, 1, 7, 3, 0,
        255, 255, 9, 13, 255, 255, 255, 255, 10, 6,
        255, 255, 11, 12
    };

    raw &= 0x1F;
    *status = (raw < 24) ? status_lookup[raw] : 255;
    return VL53L1X_OK;
}

vl53l1x_err_t vl53l1x_clear_interrupt(void)
{
    return write_byte(REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
}
