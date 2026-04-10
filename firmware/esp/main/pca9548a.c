/*
 * pca9548a.c — PCA9548A I2C multiplexer driver implementation
 *
 * Manages two PCA9548A muxes at addresses 0x70 and 0x71 on a shared
 * I2C bus. Ensures only one channel across both muxes is active at
 * any time to prevent I2C address collisions between sensors.
 */

#include "pca9548a.h"
#include "esp_log.h"

static const char *TAG = "PCA9548A";

#define I2C_TIMEOUT_MS  100

/* Device handles for both muxes */
static i2c_master_dev_handle_t s_mux0_handle;  /* 0x70 */
static i2c_master_dev_handle_t s_mux1_handle;  /* 0x71 */

/* Helper: get the device handle for a given mux address */
static i2c_master_dev_handle_t get_handle(uint8_t mux_addr)
{
    return (mux_addr == PCA9548A_MUX1_ADDR) ? s_mux1_handle : s_mux0_handle;
}

/* Helper: get the handle for the OTHER mux */
static i2c_master_dev_handle_t get_other_handle(uint8_t mux_addr)
{
    return (mux_addr == PCA9548A_MUX1_ADDR) ? s_mux0_handle : s_mux1_handle;
}

/* Helper: write a channel bitmask to a mux */
static esp_err_t write_mux(i2c_master_dev_handle_t handle, uint8_t value)
{
    return i2c_master_transmit(handle, &value, 1, I2C_TIMEOUT_MS);
}

esp_err_t pca9548a_init(i2c_master_bus_handle_t bus)
{
    esp_err_t rc;

    /* Add MUX0 (0x70) to the I2C bus */
    i2c_device_config_t mux0_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = PCA9548A_MUX0_ADDR,
        .scl_speed_hz    = 400000,
    };
    rc = i2c_master_bus_add_device(bus, &mux0_cfg, &s_mux0_handle);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add MUX0 (0x%02X): %s",
                 PCA9548A_MUX0_ADDR, esp_err_to_name(rc));
        return rc;
    }

    /* Add MUX1 (0x71) to the I2C bus */
    i2c_device_config_t mux1_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = PCA9548A_MUX1_ADDR,
        .scl_speed_hz    = 400000,
    };
    rc = i2c_master_bus_add_device(bus, &mux1_cfg, &s_mux1_handle);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add MUX1 (0x%02X): %s",
                 PCA9548A_MUX1_ADDR, esp_err_to_name(rc));
        return rc;
    }

    /* Disable all channels on both muxes to start clean */
    pca9548a_disable_all();

    ESP_LOGI(TAG, "Multiplexers initialized (MUX0=0x%02X, MUX1=0x%02X)",
             PCA9548A_MUX0_ADDR, PCA9548A_MUX1_ADDR);
    return ESP_OK;
}

esp_err_t pca9548a_select(uint8_t mux_addr, uint8_t channel)
{
    if (channel > 7) {
        ESP_LOGE(TAG, "Invalid channel %d (must be 0-7)", channel);
        return ESP_ERR_INVALID_ARG;
    }

    /* Step 1: Disable ALL channels on the OTHER mux.
     * This prevents two sensors at address 0x29 from being on the
     * bus simultaneously, which would cause I2C collisions. */
    esp_err_t rc = write_mux(get_other_handle(mux_addr), 0x00);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "Failed to disable other mux: %s", esp_err_to_name(rc));
        /* Continue anyway — the target mux select might still work */
    }

    /* Step 2: Enable only the requested channel on the target mux */
    uint8_t bitmask = (1 << channel);
    rc = write_mux(get_handle(mux_addr), bitmask);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select mux 0x%02X ch %d: %s",
                 mux_addr, channel, esp_err_to_name(rc));
    }
    return rc;
}

esp_err_t pca9548a_disable_all(void)
{
    esp_err_t rc0 = write_mux(s_mux0_handle, 0x00);
    esp_err_t rc1 = write_mux(s_mux1_handle, 0x00);
    return (rc0 != ESP_OK) ? rc0 : rc1;
}
