/**
 * I2C0 总线管理实现 — 全局递归互斥锁 + 运行时总线恢复
 */
#include "I2CBus.h"
#include <Wire.h>
#include <driver/gpio.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t _i2cMutex = nullptr;

// 连续失败阈值: 超过则触发总线恢复 (50Hz 轮询下 ≈ 100ms 内连续失败即恢复)
static const uint32_t I2C_ERROR_THRESHOLD = 5;
static uint32_t       _errorCount    = 0;
static uint32_t       _lastRecoverMs = 0;

void i2cBusInit() {
    if (!_i2cMutex) _i2cMutex = xSemaphoreCreateRecursiveMutex();
}

void i2cBusLock() {
    if (!_i2cMutex) i2cBusInit();
    xSemaphoreTakeRecursive(_i2cMutex, portMAX_DELAY);
}

void i2cBusUnlock() {
    if (_i2cMutex) xSemaphoreGiveRecursive(_i2cMutex);
}

bool i2cBusTryLock() {
    if (!_i2cMutex) i2cBusInit();
    return xSemaphoreTakeRecursive(_i2cMutex, 0) == pdTRUE;
}

// 翻转 SCL 释放被从机拉死的 SDA + (重新)初始化 Wire
void i2cBusRecoverNow() {
    // 检测 SDA 是否被拉死 (仅异常时才翻转, 避免无谓扰动总线)
    pinMode(I2C0_SDA_PIN, INPUT_PULLUP);
    if (digitalRead(I2C0_SDA_PIN) == HIGH) {
        // 总线空闲, 只需 (重新)初始化 Wire 清理可能存在的脏状态
        Wire.end();
        Wire.begin(I2C0_SDA_PIN, I2C0_SCL_PIN);
        Wire.setClock(BSP_I2C0_HZ);
        return;
    }

    // SDA 被拉死: 手动翻转 SCL 把从机时钟出去 + 发 STOP, 释放 SDA
    pinMode(I2C0_SCL_PIN, OUTPUT_OPEN_DRAIN);
    pinMode(I2C0_SDA_PIN, OUTPUT_OPEN_DRAIN);
    digitalWrite(I2C0_SCL_PIN, HIGH);
    digitalWrite(I2C0_SDA_PIN, HIGH);

    // 最多翻转 9 次 SCL, 让拉死总线的从机把剩余数据位吐完并释放 SDA
    for (int i = 0; i < 9; i++) {
        digitalWrite(I2C0_SCL_PIN, LOW);
        delayMicroseconds(5);
        digitalWrite(I2C0_SCL_PIN, HIGH);
        delayMicroseconds(5);
    }

    // 发 STOP 条件 (SCL 高时 SDA 由低拉高)
    digitalWrite(I2C0_SCL_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(I2C0_SDA_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(I2C0_SCL_PIN, HIGH);
    delayMicroseconds(5);
    digitalWrite(I2C0_SDA_PIN, HIGH);
    delayMicroseconds(5);

    // 重新初始化 Wire
    Wire.end();
    Wire.begin(I2C0_SDA_PIN, I2C0_SCL_PIN);
    Wire.setClock(BSP_I2C0_HZ);

    Serial.println("[I2CBus] 运行时总线恢复: SDA 拉死已释放, Wire 重新初始化");
}

void i2cBusReportError() {
    if (++_errorCount >= I2C_ERROR_THRESHOLD) {
        _errorCount = 0;
        uint32_t now = millis();
        if (now - _lastRecoverMs < 500) return;   // 冷却: 500ms 内不重复恢复
        _lastRecoverMs = now;

        i2cBusLock();      // 递归锁: 恢复时独占总线
        i2cBusRecoverNow();
        i2cBusUnlock();
    }
}

void i2cBusReportSuccess() {
    _errorCount = 0;
}
