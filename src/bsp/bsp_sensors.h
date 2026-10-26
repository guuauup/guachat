/**
 * bsp_sensors.h — I2C0 传感器集合
 *
 * QMC5883P 地磁 / LSM6DS3TRC IMU / SHT30 温湿度 / BMP581 气压 / PCF8563T RTC
 */
#ifndef BSP_SENSORS_H
#define BSP_SENSORS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool mag_ok;
    bool imu_ok;
    bool env_ok;
    bool baro_ok;
    bool rtc_ok;

    float accel_mps2[3];
    float gyro_rads[3];
    float mag[3];            /* 已校准（若 NVS 有校准参数），否则原始 counts */
    float heading_deg;       /* 磁北 0..360 */
    float pitch_deg;
    float roll_deg;

    float temperature_c;
    float humidity_pct;
    float dew_point_c;
    float pressure_pa;

    uint16_t rtc_year;
    uint8_t  rtc_month;
    uint8_t  rtc_day;
    uint8_t  rtc_hour;
    uint8_t  rtc_minute;
    uint8_t  rtc_second;
    bool     rtc_voltage_low;
} bsp_sensors_info_t;

/** 初始化全部传感器；返回至少一个设备可用 */
bool bsp_sensors_init(void);

/** 采样一次（I2C 操作，约几 ms；不要在 LVGL 高频定时器里每帧调用） */
bool bsp_sensors_read(bsp_sensors_info_t *out);

#ifdef __cplusplus
}
#endif

#endif /* BSP_SENSORS_H */
