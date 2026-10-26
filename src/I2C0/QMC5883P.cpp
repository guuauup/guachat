/**
 * QMC5883P 地磁计驱动实现
 *
 * I2C0 总线与地址见 bsp_pins.h
 */
#include "QMC5883P.h"
#include "I2CBus.h"

#include "bsp/bsp_pins.h"

QMC5883PManager* QMC5883PManager::_instance = nullptr;

QMC5883PManager* QMC5883PManager::getInstance() {
    if (!_instance) _instance = new QMC5883PManager();
    return _instance;
}

bool QMC5883PManager::begin() {
    Serial.print("[QMC5883P]...");

    // 上电/软复位后 QMC 可能未就绪、或总线残留脏状态 (SDA 被拉死),
    // 导致开机偶发 NOT FOUND → hasMag 恒为 0, 只有断电重开才恢复。
    // 这里最多重试 3 次: 每次先等传感器稳定 + 恢复总线再重新探测。
    for (int attempt = 0; attempt < 3; attempt++) {
        if (_sensor.begin(BSP_I2C_ADDR_QMC5883P, &Wire)) {
            // 配置: 连续模式, 50Hz ODR, OSR=8, ±2 Gauss
            // (50Hz 提供较快的动态响应，适合与陀螺仪做航向融合)
            _sensor.setMode(QMC5883P_MODE_CONTINUOUS);
            _sensor.setODR(QMC5883P_ODR_50HZ);
            _sensor.setOSR(QMC5883P_OSR_8);
            _sensor.setRange(QMC5883P_RANGE_2G);
            _sensor.setSetResetMode(QMC5883P_SETRESET_ON);

            _ready = true;
            if (attempt > 0) Serial.printf(" (重试 %d 次后成功)", attempt);
            Serial.println(" OK (50Hz, ±2G)");
            return true;
        }
        if (attempt < 2) {
            delay(60);               // 等待 QMC 上电稳定
            i2cBusRecoverNow();      // 清理可能拉死的 SDA + 重初始化 Wire
        }
    }

    Serial.println(" NOT FOUND");
    _ready = false;
    return false;
}

bool QMC5883PManager::readRaw(int16_t &x, int16_t &y, int16_t &z) {
    if (!_ready) return false;
    i2cBusLock();
    bool ok = _sensor.getRawMagnetic(&x, &y, &z);
    if (ok) i2cBusReportSuccess(); else i2cBusReportError();
    i2cBusUnlock();
    return ok;
}

bool QMC5883PManager::readCalibrated(float &x, float &y, float &z) {
    int16_t rx, ry, rz;
    if (!readRaw(rx, ry, rz)) return false;

    // mag_cal = W · (raw - offset): 全椭球软铁校正 (含离轴交叉项)
    float dx = (float)rx - _calib.offset_x;
    float dy = (float)ry - _calib.offset_y;
    float dz = (float)rz - _calib.offset_z;

    x = _calib.W00 * dx + _calib.W01 * dy + _calib.W02 * dz;
    y = _calib.W10 * dx + _calib.W11 * dy + _calib.W12 * dz;
    z = _calib.W20 * dx + _calib.W21 * dy + _calib.W22 * dz;
    return true;
}

void QMC5883PManager::setCalibration(const MagCalibData &calib) {
    _calib = calib;
}

void QMC5883PManager::sleep() {
    if (!_ready) return;
    i2cBusLock();
    _sensor.setMode(QMC5883P_MODE_SUSPEND);  // 22µA
    i2cBusUnlock();
    Serial.println("[QMC5883P] Suspend");
}

void QMC5883PManager::wake() {
    if (!_ready) return;
    i2cBusLock();
    _sensor.setMode(QMC5883P_MODE_CONTINUOUS);  // restore ~310µA
    i2cBusUnlock();
    Serial.println("[QMC5883P] Wake");
}
