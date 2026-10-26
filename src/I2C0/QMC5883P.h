/**
 * QMC5883P 地磁计硬件封装 (I2C0, 地址见 BSP_I2C_ADDR_QMC5883P)
 *
 * 封装 Adafruit_QMC5883P 库为单例模式，支持硬铁校准参数注入
 */
#ifndef I2C0_QMC5883P_H
#define I2C0_QMC5883P_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_QMC5883P.h>

struct MagCalibData {
    bool  valid = false;

    // 硬铁偏移 (原始 counts)
    float offset_x = 0, offset_y = 0, offset_z = 0;

    // 软铁 3×3 变换矩阵 (对称, 行主序): mag_cal = W · (raw - offset)
    // 对角项 ≈ 原 scale (~1/地球场半径), 非对角项校正离轴软铁畸变
    float W00 = 1, W01 = 0, W02 = 0;
    float W10 = 0, W11 = 1, W12 = 0;
    float W20 = 0, W21 = 0, W22 = 1;
};

class QMC5883PManager {
public:
    static QMC5883PManager* getInstance();

    bool begin();
    bool isReady() const { return _ready; }

    // 读取原始磁场值 (16-bit ADC)
    bool readRaw(int16_t &x, int16_t &y, int16_t &z);

    // 读取校准后的磁场值 (单位: 归一化, 无单位)
    bool readCalibrated(float &x, float &y, float &z);

    // 注入校准参数 (从 NVS 读取后调用)
    void setCalibration(const MagCalibData &calib);
    const MagCalibData& getCalibration() const { return _calib; }

    // 低功耗: Suspend 模式 (22µA), 唤醒后 restore
    void sleep();
    void wake();

private:
    QMC5883PManager() {}
    static QMC5883PManager* _instance;

    Adafruit_QMC5883P _sensor;
    MagCalibData      _calib;
    bool              _ready = false;
};

#endif
