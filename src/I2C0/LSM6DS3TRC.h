/**
 * LSM6DS3TR-C IMU 硬件封装 (I2C0, 地址见 BSP_I2C_ADDR_LSM6DS3TRC)
 *
 * 封装 Adafruit_LSM6DS3TRC 库为单例模式,
 * 提供加速度计和陀螺仪原始数据读取
 */
#ifndef I2C0_LSM6DS3TRC_H
#define I2C0_LSM6DS3TRC_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LSM6DS3TRC.h>

class LSM6DS3TRCManager {
public:
    static LSM6DS3TRCManager* getInstance();

    bool begin();
    bool isReady() const { return _ready; }

    // 读取加速度 (m/s²)
    bool readAccel(float &x, float &y, float &z);

    // 读取陀螺仪 (rad/s)
    bool readGyro(float &x, float &y, float &z);

    // 同时读取加速度和陀螺仪
    bool readAll(float &ax, float &ay, float &az,
                 float &gx, float &gy, float &gz);

    // 低功耗: 加速度计+陀螺仪 shutdown (~3µA), 唤醒后 restore
    void sleep();
    void wake();

private:
    LSM6DS3TRCManager() {}
    static LSM6DS3TRCManager* _instance;

    Adafruit_LSM6DS3TRC _sensor;
    bool _ready = false;
};

#endif
