/**
 * PCF8563T RTC 实时时钟驱动 — I2C0 总线 (地址见 BSP_I2C_ADDR_PCF8563T)
 *
 * BCD 编码寄存器，带世纪标志和电压低检测
 *
 * 直接操作 Wire（I2C0），不依赖外部库
 */
#ifndef PCF8563T_DRIVER_H
#define PCF8563T_DRIVER_H

#include <Arduino.h>
#include <Wire.h>

#include "bsp/bsp_pins.h"

class PCF8563TDriver
{
public:
  /** 时间数据结构 */
  struct DateTime
  {
    uint8_t second;   // 0-59
    uint8_t minute;   // 0-59
    uint8_t hour;     // 0-23
    uint8_t day;      // 1-31
    uint8_t weekday;  // 0-6 (0=周日)
    uint8_t month;    // 1-12
    uint16_t year;    // 2000-2099
  };

  /** 构造 (默认地址见 bsp_pins.h) */
  PCF8563TDriver(uint8_t addr = BSP_I2C_ADDR_PCF8563T);

  /** 检测芯片是否存在 */
  bool begin();

  /** 读取当前时间 */
  bool readTime(DateTime &out);

  /** 写入时间 */
  bool writeTime(const DateTime &dt);

  /** 检查电压低标志 (VL=1 表示时钟可能不准确) */
  bool isVoltageLow();

  /** 将 DateTime 转为 UTC 总秒数 (自 2000-01-01 00:00:00), 用于时间差比较 */
  static uint32_t toEpoch(const DateTime &dt);

  /** 检查是否连接 */
  bool isConnected() const { return _connected; }

  /** 获取最后错误码 */
  int lastError() const { return _error; }

private:
  TwoWire *_wire;
  uint8_t  _addr;
  bool     _connected;
  int      _error;

  bool readRegisters(uint8_t startReg, uint8_t *data, uint8_t len);
  bool writeRegisters(uint8_t startReg, const uint8_t *data, uint8_t len);

  static uint8_t  dec2bcd(uint8_t val);
  static uint8_t  bcd2dec(uint8_t val);

  // 寄存器地址
  static constexpr uint8_t REG_CTRL1     = 0x00;  // 控制寄存器1
  static constexpr uint8_t REG_CTRL2     = 0x01;  // 控制寄存器2
  static constexpr uint8_t REG_VL_SEC    = 0x02;  // 秒 (bit7=VL)
  static constexpr uint8_t REG_MIN       = 0x03;  // 分
  static constexpr uint8_t REG_HOUR      = 0x04;  // 时
  static constexpr uint8_t REG_DAY       = 0x05;  // 日
  static constexpr uint8_t REG_WEEKDAY   = 0x06;  // 星期
  static constexpr uint8_t REG_MONTH     = 0x07;  // 月 (bit7=世纪)
  static constexpr uint8_t REG_YEAR      = 0x08;  // 年 (0-99)
};

#endif
