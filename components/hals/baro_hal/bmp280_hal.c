#include <stdlib.h>
#include "bmp280_hal.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bmp280_hal";

#define REG_CALIB      0x88   // 24 bytes of calibration data
#define REG_ID         0xD0
#define REG_RESET      0xE0
#define REG_STATUS     0xF3
#define REG_CTRL_MEAS  0xF4
#define REG_CONFIG     0xF5
#define REG_PRESS_MSB  0xF7   // 6 bytes: press[3], temp[3]

#define CHIP_ID_BMP280     0x58   // early samples report 0x56 / 0x57
#define RESET_MAGIC        0xB6
#define STATUS_MEASURING   (1 << 3)
#define STATUS_IM_UPDATE   (1 << 0)
#define I2C_TIMEOUT_MS     100

struct bmp280_hal {
    i2c_master_dev_handle_t dev;
    bmp280_hal_config_t     cfg;      /* kept so reset/read can reuse the settings */

    uint16_t dig_T1;  int16_t dig_T2, dig_T3;
    uint16_t dig_P1;  int16_t dig_P2, dig_P3, dig_P4, dig_P5,
                              dig_P6, dig_P7, dig_P8, dig_P9;
    int32_t  t_fine;                  /* temperature carry-over used by the pressure maths */
};

// Low level register write
static esp_err_t write_reg(struct bmp280_hal *h, uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(h->dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

// Low level register read
static esp_err_t read_regs(struct bmp280_hal *h, uint8_t reg, uint8_t *dst, size_t len)
{
    return i2c_master_transmit_receive(h->dev, &reg, 1, dst, len, I2C_TIMEOUT_MS);
}

// Write config + ctrl_meas. In forced mode the chip stays asleep here;
// each read triggers one measurement.
static esp_err_t apply_config(struct bmp280_hal *h)
{
    const bmp280_hal_config_t *c = &h->cfg;

    uint8_t config = (c->standby << 5) | (c->filter << 2);
    ESP_RETURN_ON_ERROR(write_reg(h, REG_CONFIG, config), TAG, "write config");

    uint8_t mode = (c->mode == BMP280_MODE_NORMAL) ? BMP280_MODE_NORMAL : BMP280_MODE_SLEEP;
    uint8_t ctrl = (c->osrs_temp << 5) | (c->osrs_press << 2) | mode;
    return write_reg(h, REG_CTRL_MEAS, ctrl);
}

// Reset the chip and wait until it has reloaded its calibration data
static esp_err_t reset_chip(struct bmp280_hal *h)
{
    ESP_RETURN_ON_ERROR(write_reg(h, REG_RESET, RESET_MAGIC), TAG, "write reset");

    for (int i = 0; i < 10; i++) {                 // up to ~20 ms
        vTaskDelay(pdMS_TO_TICKS(2));
        uint8_t status;
        ESP_RETURN_ON_ERROR(read_regs(h, REG_STATUS, &status, 1), TAG, "read status");
        if (!(status & STATUS_IM_UPDATE)) return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

static uint16_t u16le(const uint8_t *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static int16_t  s16le(const uint8_t *b) { return (int16_t)u16le(b); }

// Read the factory calibration constants (little-endian, datasheet order)
static esp_err_t read_calibration(struct bmp280_hal *h)
{
    uint8_t b[24];
    ESP_RETURN_ON_ERROR(read_regs(h, REG_CALIB, b, sizeof(b)), TAG, "read calibration");

    h->dig_T1 = u16le(&b[0]);   h->dig_T2 = s16le(&b[2]);   h->dig_T3 = s16le(&b[4]);
    h->dig_P1 = u16le(&b[6]);   h->dig_P2 = s16le(&b[8]);   h->dig_P3 = s16le(&b[10]);
    h->dig_P4 = s16le(&b[12]);  h->dig_P5 = s16le(&b[14]);  h->dig_P6 = s16le(&b[16]);
    h->dig_P7 = s16le(&b[18]);  h->dig_P8 = s16le(&b[20]);  h->dig_P9 = s16le(&b[22]);
    return ESP_OK;
}

// Datasheet floating-point compensation. Temperature must run first: it sets t_fine.
static double compensate_temp(struct bmp280_hal *h, int32_t adc_T)
{
    double var1 = ((double)adc_T / 16384.0 - (double)h->dig_T1 / 1024.0) * (double)h->dig_T2;
    double d    =  (double)adc_T / 131072.0 - (double)h->dig_T1 / 8192.0;
    double var2 = d * d * (double)h->dig_T3;

    h->t_fine = (int32_t)(var1 + var2);
    return (var1 + var2) / 5120.0;
}

static double compensate_pressure(struct bmp280_hal *h, int32_t adc_P)
{
    double var1 = (double)h->t_fine / 2.0 - 64000.0;
    double var2 = var1 * var1 * (double)h->dig_P6 / 32768.0;
    var2 = var2 + var1 * (double)h->dig_P5 * 2.0;
    var2 = var2 / 4.0 + (double)h->dig_P4 * 65536.0;
    var1 = ((double)h->dig_P3 * var1 * var1 / 524288.0 + (double)h->dig_P2 * var1) / 524288.0;
    var1 = (1.0 + var1 / 32768.0) * (double)h->dig_P1;
    if (var1 == 0.0) return 0.0;                   // avoid divide by zero

    double p = 1048576.0 - (double)adc_P;
    p = (p - var2 / 4096.0) * 6250.0 / var1;
    var1 = (double)h->dig_P9 * p * p / 2147483648.0;
    var2 = p * (double)h->dig_P8 / 32768.0;
    return p + (var1 + var2 + (double)h->dig_P7) / 16.0;
}

// Worst-case forced-mode conversion time in ms (datasheet formula, rounded up)
static uint32_t measure_time_ms(const bmp280_hal_config_t *c)
{
    static const uint8_t mult[] = { 0, 1, 2, 4, 8, 16 };   // indexed by osrs value
    return 3 + (23 * (mult[c->osrs_temp] + mult[c->osrs_press])) / 10;
}

esp_err_t bmp280_hal_init(const bmp280_hal_config_t *cfg, bmp280_hal_handle_t *out)
{
    ESP_RETURN_ON_FALSE(cfg && cfg->bus && out, ESP_ERR_INVALID_ARG, TAG, "bad argument");

    struct bmp280_hal *h = calloc(1, sizeof(*h));
    ESP_RETURN_ON_FALSE(h, ESP_ERR_NO_MEM, TAG, "no memory");
    h->cfg = *cfg;

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = cfg->address,
        .scl_speed_hz    = cfg->scl_speed_hz,
    };
    esp_err_t err = i2c_master_bus_add_device(cfg->bus, &dev_cfg, &h->dev);
    if (err != ESP_OK) {
        free(h);
        return err;
    }

    err = reset_chip(h);
    if (err != ESP_OK) goto fail;

    uint8_t id = 0;
    err = read_regs(h, REG_ID, &id, 1);
    if (err != ESP_OK) goto fail;
    if (id < 0x56 || id > CHIP_ID_BMP280) {
        ESP_LOGE(TAG, "unexpected chip id 0x%02x (0x60 means BME280)", id);
        err = ESP_ERR_NOT_FOUND;
        goto fail;
    }

    err = read_calibration(h);
    if (err != ESP_OK) goto fail;

    err = apply_config(h);
    if (err != ESP_OK) goto fail;

    *out = h;
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
    i2c_master_bus_rm_device(h->dev);
    free(h);
    return err;
}

esp_err_t bmp280_hal_destroy(bmp280_hal_handle_t h)
{
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_ARG, TAG, "null handle");

    write_reg(h, REG_CTRL_MEAS, 0x00);             // best effort: sleep mode
    esp_err_t err = i2c_master_bus_rm_device(h->dev);
    free(h);
    return err;
}

esp_err_t bmp280_hal_read(bmp280_hal_handle_t h, bmp280_data_t *out)
{
    ESP_RETURN_ON_FALSE(h && out, ESP_ERR_INVALID_ARG, TAG, "null argument");

    if (h->cfg.mode != BMP280_MODE_NORMAL) {
        // Forced mode: trigger one measurement and wait for it
        uint8_t ctrl = (h->cfg.osrs_temp << 5) | (h->cfg.osrs_press << 2) | BMP280_MODE_FORCED;
        ESP_RETURN_ON_ERROR(write_reg(h, REG_CTRL_MEAS, ctrl), TAG, "trigger");

        vTaskDelay(pdMS_TO_TICKS(measure_time_ms(&h->cfg)) + 1);

        for (int i = 0; i < 10; i++) {             // wait for the measuring bit to clear
            uint8_t status;
            ESP_RETURN_ON_ERROR(read_regs(h, REG_STATUS, &status, 1), TAG, "read status");
            if (!(status & STATUS_MEASURING)) break;
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    uint8_t b[6];
    ESP_RETURN_ON_ERROR(read_regs(h, REG_PRESS_MSB, b, sizeof(b)), TAG, "read data");

    int32_t adc_P = ((int32_t)b[0] << 12) | ((int32_t)b[1] << 4) | (b[2] >> 4);
    int32_t adc_T = ((int32_t)b[3] << 12) | ((int32_t)b[4] << 4) | (b[5] >> 4);

    out->temperature_c = (float)compensate_temp(h, adc_T);   // must come first: sets t_fine
    out->pressure_pa   = (float)compensate_pressure(h, adc_P);
    return ESP_OK;
}

esp_err_t bmp280_hal_soft_reset(bmp280_hal_handle_t h)
{
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_ARG, TAG, "null handle");

    ESP_RETURN_ON_ERROR(reset_chip(h), TAG, "reset");
    return apply_config(h);                        // a reset wipes the settings, so restore them
}