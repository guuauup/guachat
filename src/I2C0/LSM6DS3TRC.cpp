/**
 * LSM6DS3TR-C IMU 驱动实现
 *
 * I2C0 总线与地址见 bsp_pins.h
 */
#include "LSM6DS3TRC.h"
#include "I2CBus.h"

#include "bsp/bsp_pins.h"

LSM6DS3TRCManager* LSM6DS3TRCManager::_instance = nullptr;

LSM6DS3TRCManager* LSM6DS3TRCManager::getInstance() {
    if (!_instance) _instance = new LSM6DS3TRCManager();
    return _instance;
}

bool LSM6DS3TRCManager::begin() {
    Serial.print("[LSM6DS3TRC]...");

    i2cBusLock();
    bool ok = _sensor.begin_I2C(BSP_I2C_ADDR_LSM6DS3TRC, &Wire);
    if (ok) {
        // 加速度计: 52Hz, ±16G（留足冲击/振动量程余量）
        _sensor.setAccelDataRate(LSM6DS_RATE_52_HZ);
        _sensor.setAccelRange(LSM6DS_ACCEL_RANGE_16_G);

        // 陀螺仪: 52Hz, ±250dps (最灵敏)
        _sensor.setGyroDataRate(LSM6DS_RATE_52_HZ);
        _sensor.setGyroRange(LSM6DS_GYRO_RANGE_250_DPS);
    }
    i2cBusUnlock();

    if (!ok) {
        Serial.println(" NOT FOUND");
        _ready = false;
        return false;
    }

    _ready = true;
    Serial.println(" OK (52Hz accel+gyro)");
    return true;
}

bool LSM6DS3TRCManager::readAccel(float &x, float &y, float &z) {
    if (!_ready) return false;

    i2cBusLock();
    sensors_event_t accel, gyro, temp;
    bool ok = _sensor.getEvent(&accel, &gyro, &temp);
    if (ok) i2cBusReportSuccess(); else i2cBusReportError();
    i2cBusUnlock();
    if (!ok) return false;

    x = accel.acceleration.x;  // m/s²
    y = accel.acceleration.y;
    z = accel.acceleration.z;
    return true;
}

bool LSM6DS3TRCManager::readGyro(float &x, float &y, float &z) {
    if (!_ready) return false;

    i2cBusLock();
    sensors_event_t accel, gyro, temp;
    bool ok = _sensor.getEvent(&accel, &gyro, &temp);
    if (ok) i2cBusReportSuccess(); else i2cBusReportError();
    i2cBusUnlock();
    if (!ok) return false;

    x = gyro.gyro.x;  // rad/s
    y = gyro.gyro.y;
    z = gyro.gyro.z;
    return true;
}

bool LSM6DS3TRCManager::readAll(float &ax, float &ay, float &az,
                                float &gx, float &gy, float &gz) {
    if (!_ready) return false;

    i2cBusLock();
    sensors_event_t accel, gyro, temp;
    bool ok = _sensor.getEvent(&accel, &gyro, &temp);
    if (ok) i2cBusReportSuccess(); else i2cBusReportError();
    i2cBusUnlock();
    if (!ok) return false;

    ax = accel.acceleration.x;
    ay = accel.acceleration.y;
    az = accel.acceleration.z;
    gx = gyro.gyro.x;
    gy = gyro.gyro.y;
    gz = gyro.gyro.z;
    return true;
}

void LSM6DS3TRCManager::sleep() {
    if (!_ready) return;
    i2cBusLock();
    _sensor.setAccelDataRate(LSM6DS_RATE_SHUTDOWN);
    _sensor.setGyroDataRate(LSM6DS_RATE_SHUTDOWN);
    i2cBusUnlock();
    Serial.println("[LSM6DS3TRC] Sleep");
}

void LSM6DS3TRCManager::wake() {
    if (!_ready) return;
    i2cBusLock();
    _sensor.setAccelDataRate(LSM6DS_RATE_52_HZ);
    _sensor.setAccelRange(LSM6DS_ACCEL_RANGE_16_G);
    _sensor.setGyroDataRate(LSM6DS_RATE_52_HZ);
    _sensor.setGyroRange(LSM6DS_GYRO_RANGE_250_DPS);
    i2cBusUnlock();
    Serial.println("[LSM6DS3TRC] Wake");
}
