/**
 * SHT30 温湿度传感器驱动实现
 *
 * I2C0: SCL=GPIO1, SDA=GPIO2 (由 TCA9535Manager 初始化)
 * 地址: 0x44
 */
#include "SHT30.h"
#include "I2CBus.h"
#include <math.h>

SHT30Manager* SHT30Manager::_instance = nullptr;

SHT30Manager* SHT30Manager::getInstance() {
    if (!_instance) _instance = new SHT30Manager();
    return _instance;
}

bool SHT30Manager::begin() {
    Serial.print("[SHT30]...");

    _sht.mSensorType = SHTSensor::SHT3X;
    if (!_sht.init(Wire)) {
        Serial.println(" NOT FOUND");
        _ready = false;
        return false;
    }

    _temperature = _sht.getTemperature();
    _humidity    = _sht.getHumidity();

    _ready = true;
    Serial.printf(" OK  T=%.1f°C  RH=%.1f%%\n", _temperature, _humidity);
    return true;
}

bool SHT30Manager::readSensor() {
    if (!_ready) return false;

    i2cBusLock();
    bool ok = _sht.readSample();
    if (ok) {
        i2cBusReportSuccess();
        _temperature = _sht.getTemperature();
        _humidity    = _sht.getHumidity();
    } else {
        i2cBusReportError();
    }
    i2cBusUnlock();
    return ok;
}

// ============================================================
// 露点温度 — Magnus 公式
// ============================================================
float SHT30Manager::computeDewPoint(float temperature, float humidity) {
    if (humidity <= 0.0f || humidity > 100.0f) return NAN;

    constexpr float A = 17.27f;
    constexpr float B = 237.7f;

    float gamma = (A * temperature) / (B + temperature) + logf(humidity / 100.0f);
    return (B * gamma) / (A - gamma);
}
