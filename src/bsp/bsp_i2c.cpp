/**
 * bsp_i2c.cpp — I2C0 扫描
 */
#include "bsp_i2c.h"

#include <Arduino.h>
#include <Wire.h>

#include "I2C0/I2CBus.h"

void bsp_i2c_scan(void)
{
    i2cBusLock();
    Serial.println("[BSP] I2C0 scan:");

    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  0x%02X\n", addr);
            ++found;
        }
    }

    i2cBusUnlock();
    Serial.printf("[BSP] I2C0: %u device(s)\n", found);
}

bool bsp_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *value)
{
    if (!value) {
        return false;
    }

    i2cBusLock();
    Wire.beginTransmission(addr);
    Wire.write(reg);
    const bool ok = (Wire.endTransmission(false) == 0) &&
                    (Wire.requestFrom(addr, (uint8_t)1) == 1);
    if (ok) {
        *value = Wire.read();
    }
    i2cBusUnlock();
    return ok;
}
