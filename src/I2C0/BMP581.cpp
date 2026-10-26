/**
 * BMP581 气压传感器 (I2C0, 地址见 BSP_I2C_ADDR_BMP581)
 *
 * 配置: 温度 1x, 气压 16x, IIR coeff_3, ODR 1Hz, Normal 模式
 * FIFO 关闭 (默认), INT 未接
 */
#include "BMP581.h"
#include "I2CBus.h"

#include "bsp/bsp_pins.h"

BMP581Manager* BMP581Manager::_instance = nullptr;

BMP581Manager* BMP581Manager::getInstance() {
    if (!_instance) _instance = new BMP581Manager();
    return _instance;
}

bool BMP581Manager::begin() {
    Serial.print("[BMP581]...");

    // 1) 基础初始化
    if (!_bmp.begin(BSP_I2C_ADDR_BMP581, &Wire)) {
        Serial.println(" NOT FOUND");
        _ready = false;
        return false;
    }

    // 2) STANDBY → 修改配置
    _bmp.setPowerMode(BMP5XX_POWERMODE_STANDBY);
    delay(5);

    _bmp.setTemperatureOversampling(BMP5XX_OVERSAMPLING_1X);
    _bmp.setPressureOversampling(BMP5XX_OVERSAMPLING_16X);
    _bmp.setIIRFilterCoeff(BMP5XX_IIR_FILTER_COEFF_3);
    _bmp.setOutputDataRate(BMP5XX_ODR_01_HZ);

    // 3) 关闭中断 (INT 引脚未接)
    _bmp.configureInterrupt(BMP5XX_INTERRUPT_PULSED,
                            BMP5XX_INTERRUPT_ACTIVE_HIGH,
                            BMP5XX_INTERRUPT_PUSH_PULL,
                            0, false);

    // 4) Normal 模式
    _bmp.setPowerMode(BMP5XX_POWERMODE_NORMAL);

    _ready = true;
    Serial.println(" OK");
    return true;
}

bool BMP581Manager::performReading() {
    if (!_ready) return false;

    i2cBusLock();
    bool ok = _bmp.performReading();
    if (ok) {
        i2cBusReportSuccess();
        _temperature = _bmp.temperature;
        _pressure    = _bmp.pressure;
    } else {
        i2cBusReportError();
    }
    i2cBusUnlock();
    return ok;
}

// ============================================================
// configureOutput — 通用 ODR / 功耗模式设置
//   STANDBY → 改 ODR → 目标模式，避免切换时产生脏样本
// ============================================================
bool BMP581Manager::configureOutput(bmp5xx_odr_t odr, bmp5xx_powermode_t mode) {
    if (!_ready) return false;

    i2cBusLock();
    bool ok = _bmp.setPowerMode(BMP5XX_POWERMODE_STANDBY);
    if (ok) {
        delay(5);   // 等 STANDBY 生效后再改配置
        ok = _bmp.setOutputDataRate(odr);
        if (ok) ok = _bmp.setPowerMode(mode);
    }
    if (ok) i2cBusReportSuccess(); else i2cBusReportError();
    i2cBusUnlock();
    return ok;
}
