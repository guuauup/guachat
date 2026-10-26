/**
 * BMP581 气压传感器 (I2C0, 地址见 BSP_I2C_ADDR_BMP581)
 *
 * 配置: 温度 1x, 气压 16x, IIR coeff_3, ODR 1Hz, Normal 模式
 */
#ifndef I2C0_BMP581_H
#define I2C0_BMP581_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP5xx.h>

class BMP581Manager {
public:
    static BMP581Manager* getInstance();
    bool begin();
    bool isReady() const { return _ready; }
    bool performReading();
    float getTemperature() const { return _temperature; }
    float getPressure()    const { return _pressure; }

    /** 重设 ODR 与功耗模式，调用方按自己的采样需求选择
     *  例: configureOutput(BMP5XX_ODR_50_HZ, BMP5XX_POWERMODE_CONTINUOUS) */
    bool configureOutput(bmp5xx_odr_t odr, bmp5xx_powermode_t mode);

private:
    BMP581Manager() {}
    static BMP581Manager* _instance;
    Adafruit_BMP5xx _bmp;
    bool   _ready       = false;
    float  _temperature = NAN;
    float  _pressure    = NAN;
};

#endif
