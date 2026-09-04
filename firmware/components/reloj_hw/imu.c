#include "imu.h"
#include "esp_log.h"
#include "qmi8658.h"

static const char *TAG = "imu";
static qmi8658_dev_t dev;
static bool listo;

esp_err_t imu_init(i2c_master_bus_handle_t bus)
{
    esp_err_t r = qmi8658_init(&dev, bus, QMI8658_ADDRESS_HIGH);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "QMI8658 no responde (%s)", esp_err_to_name(r));
        return r;
    }
    qmi8658_set_accel_range(&dev, QMI8658_ACCEL_RANGE_8G);
    qmi8658_set_accel_odr(&dev, QMI8658_ACCEL_ODR_125HZ);
    qmi8658_set_accel_unit_mps2(&dev, true);
    qmi8658_set_gyro_range(&dev, QMI8658_GYRO_RANGE_512DPS);
    qmi8658_set_gyro_odr(&dev, QMI8658_GYRO_ODR_125HZ);
    qmi8658_enable_accel(&dev, true);
    qmi8658_enable_gyro(&dev, true);
    listo = true;
    ESP_LOGI(TAG, "QMI8658 listo");
    return ESP_OK;
}

bool imu_leer(float *ax, float *ay, float *az, float *gx, float *gy, float *gz)
{
    if (!listo) {
        return false;
    }
    return qmi8658_read_accel(&dev, ax, ay, az) == ESP_OK && qmi8658_read_gyro(&dev, gx, gy, gz) == ESP_OK;
}

bool imu_disponible(void) { return listo; }
