/**
 * bsp_power.cpp — MAX17055 薄封装
 *
 * 只做电量计初始化与批量读数，不包含 NVS 设置、低压关机、LVGL 刷新。
 * 应用需要低压策略时自行基于 bsp_power_read() 实现。
 */
#include "bsp_power.h"
#include "bsp_pins.h"

#include <Arduino.h>
#include <Wire.h>
#include <MAX17055.h>

#include "I2C0/I2CBus.h"

namespace {

constexpr uint8_t REG_DESIGNCAP = 0x18;
constexpr uint8_t REG_VEMPTY    = 0x3A;

MAX17055 s_fuel;
bool     s_ready = false;

void write_reg16(uint8_t reg, uint16_t val)
{
    i2cBusLock();
    Wire.beginTransmission(BSP_I2C_ADDR_MAX17055);
    Wire.write(reg);
    Wire.write((uint8_t)(val & 0xFF));
    Wire.write((uint8_t)(val >> 8));
    const uint8_t err = Wire.endTransmission();
    if (err == 0) {
        i2cBusReportSuccess();
    } else {
        i2cBusReportError();
    }
    i2cBusUnlock();
}

uint16_t read_reg16(uint8_t reg)
{
    uint16_t result = 0;
    i2cBusLock();
    Wire.beginTransmission(BSP_I2C_ADDR_MAX17055);
    Wire.write(reg);
    const uint8_t err = Wire.endTransmission();
    if (err == 0 && Wire.requestFrom((uint8_t)BSP_I2C_ADDR_MAX17055, (uint8_t)2) >= 2) {
        const uint8_t lo = Wire.read();
        const uint8_t hi = Wire.read();
        result = ((uint16_t)hi << 8) | lo;
        i2cBusReportSuccess();
    } else {
        i2cBusReportError();
    }
    i2cBusUnlock();
    return result;
}

}  // namespace

bool bsp_power_init(void)
{
    if (s_ready) {
        return true;
    }

    i2cBusInit();

    i2cBusLock();
    bool ok = s_fuel.Detect_Only();
    if (ok) {
        ok = s_fuel.Ensure_Base_Config();
    }
    i2cBusUnlock();

    if (!ok) {
        Serial.printf("[BSP] power: MAX17055 not found (%s)\n",
                      s_fuel.Get_Error_Message());
        return false;
    }

    /* 设计容量：0x18 = mAh × 2 */
    write_reg16(REG_DESIGNCAP, (uint16_t)(BSP_BATTERY_DESIGN_MAH * 2));

    /* 空电电压：VEmpty 高 9 位（10mV/LSB），保留低 7 位 VRecovery */
    uint16_t vempty = read_reg16(REG_VEMPTY);
    vempty = (uint16_t)((vempty & 0x007F) |
                        (((uint16_t)(BSP_BATTERY_EMPTY_MV / 10)) << 7));
    write_reg16(REG_VEMPTY, vempty);

    s_ready = true;
    Serial.printf("[BSP] power: MAX17055 OK, %d mAh\n", BSP_BATTERY_DESIGN_MAH);
    return true;
}

bool bsp_power_read(bsp_power_info_t *out)
{
    if (!out) {
        return false;
    }
    *out = {};

    if (!s_ready) {
        return false;
    }

    MAX17055::BatteryData_t data;
    i2cBusLock();
    bool ok = s_fuel.Is_Ready();
    if (ok) {
        ok = s_fuel.Read_Battery_Data(data);
    }
    if (ok) {
        i2cBusReportSuccess();
    } else {
        i2cBusReportError();
    }
    i2cBusUnlock();

    if (!ok || !data.valid) {
        return false;
    }

    out->valid          = true;
    out->voltage_mv     = data.voltage_mV;
    out->current_ma     = data.current_mA;
    out->soc_percent    = data.soc_percent;
    out->remaining_mah  = data.capacity_mAh;
    out->design_mah     = data.design_capacity_mAh;
    out->temperature_c  = data.temperature_C;
    out->charging       = data.current_mA > 50;
    return true;
}
