#pragma once
// IMU QMI8658 (I2C 0x6B, bus compartido del BSP): acelerómetro y giroscopio para la app "Sensores".
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t imu_init(i2c_master_bus_handle_t bus);
// Aceleración en m/s² y velocidad angular en °/s. false si el chip no respondió.
bool imu_leer(float *ax, float *ay, float *az, float *gx, float *gy, float *gz);
bool imu_disponible(void);
#ifdef __cplusplus
}
#endif
