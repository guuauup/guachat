/**
 * SHT30 温湿度传感器 (I2C0, 地址 0x44)
 *
 * 封装 Sensirion arduino-sht 库为单例模式
 * 测量模式: 单次测量, 高重复性
 *
 * 露点温度: Magnus 公式 (a=17.27, b=237.7)
 */
#ifndef I2C0_SHT30_H
#define I2C0_SHT30_H

#include <Arduino.h>
#include <Wire.h>
#include <SHTSensor.h>

class SHT30Manager {
public:
    static SHT30Manager* getInstance();

    bool begin();
    bool isReady() const { return _ready; }

    /** 触发单次采样, 更新缓存 */
    bool readSensor();

    float getTemperature() const { return _temperature; }
    float getHumidity()    const { return _humidity; }

    /** Magnus 公式计算露点温度 (°C) */
    static float computeDewPoint(float temperature, float humidity);

private:
    SHT30Manager() {}
    static SHT30Manager* _instance;

    SHTSensor _sht;
    bool      _ready       = false;
    float     _temperature = NAN;
    float     _humidity    = NAN;
};

#endif
