/**
 * bsp_sensors.cpp — 传感器集合的板级封装
 */
#include "bsp_sensors.h"
#include "bsp_pins.h"

#include <Arduino.h>
#include <math.h>

#include "I2C0/SensorFusion.h"
#include "I2C0/LSM6DS3TRC.h"
#include "I2C0/QMC5883P.h"
#include "I2C0/SHT30.h"
#include "I2C0/BMP581.h"
#include "I2C0/PCF8563T.h"

namespace {

constexpr float BSP_RAD_TO_DEG = 57.2957795f;

PCF8563TDriver s_rtc(BSP_I2C_ADDR_PCF8563T);
bool s_ready = false;

}  // namespace

bool bsp_sensors_init(void)
{
    bool any = false;

    any |= SensorFusion::getInstance()->begin();
    any |= SHT30Manager::getInstance()->begin();
    any |= BMP581Manager::getInstance()->begin();
    any |= s_rtc.begin();

    s_ready = any;
    Serial.printf("[BSP] sensors: %s\n", any ? "ready" : "no device found");
    return any;
}

bool bsp_sensors_read(bsp_sensors_info_t *out)
{
    if (!out) {
        return false;
    }
    *out = {};

    if (!s_ready) {
        return false;
    }

    SensorFusion *fusion = SensorFusion::getInstance();
    fusion->update();

    out->mag_ok     = fusion->hasMag();
    out->imu_ok     = fusion->hasIMU();
    out->heading_deg = fusion->getMagneticHeading();
    out->pitch_deg   = fusion->getPitch() * BSP_RAD_TO_DEG;
    out->roll_deg    = fusion->getRoll()  * BSP_RAD_TO_DEG;

    LSM6DS3TRCManager *imu = LSM6DS3TRCManager::getInstance();
    out->imu_ok = imu->readAccel(out->accel_mps2[0], out->accel_mps2[1],
                                 out->accel_mps2[2]) && out->imu_ok;
    float gx = 0, gy = 0, gz = 0;
    if (imu->readGyro(gx, gy, gz)) {
        out->gyro_rads[0] = gx;
        out->gyro_rads[1] = gy;
        out->gyro_rads[2] = gz;
    }

    QMC5883PManager *mag = QMC5883PManager::getInstance();
    if (!mag->readCalibrated(out->mag[0], out->mag[1], out->mag[2])) {
        int16_t rx = 0, ry = 0, rz = 0;
        if (mag->readRaw(rx, ry, rz)) {
            out->mag[0] = (float)rx;
            out->mag[1] = (float)ry;
            out->mag[2] = (float)rz;
        } else {
            out->mag_ok = false;
        }
    }

    SHT30Manager *env = SHT30Manager::getInstance();
    out->env_ok = env->readSensor();
    if (out->env_ok) {
        out->temperature_c = env->getTemperature();
        out->humidity_pct  = env->getHumidity();
        out->dew_point_c   = SHT30Manager::computeDewPoint(out->temperature_c,
                                                          out->humidity_pct);
    }

    BMP581Manager *baro = BMP581Manager::getInstance();
    out->baro_ok = baro->performReading();
    if (out->baro_ok) {
        /* Adafruit_BMP5xx::pressure 单位是 hPa，BSP 统一暴露 Pa */
        out->pressure_pa = baro->getPressure() * 100.0f;
    }

    PCF8563TDriver::DateTime dt = {};
    out->rtc_ok = s_rtc.readTime(dt);
    if (out->rtc_ok) {
        out->rtc_year    = dt.year;
        out->rtc_month   = dt.month;
        out->rtc_day     = dt.day;
        out->rtc_hour    = dt.hour;
        out->rtc_minute  = dt.minute;
        out->rtc_second  = dt.second;
        out->rtc_voltage_low = s_rtc.isVoltageLow();
    }

    return out->mag_ok || out->imu_ok || out->env_ok ||
           out->baro_ok || out->rtc_ok;
}
