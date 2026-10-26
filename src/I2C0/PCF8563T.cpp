/**
 * PCF8563T RTC 实时时钟驱动实现
 *
 * I2C0 总线: Wire, 地址见 bsp_pins.h
 * BCD 编码寄存器, 带世纪位和 VL (Voltage Low) 检测
 */
#include "PCF8563T.h"
#include "I2CBus.h"

PCF8563TDriver::PCF8563TDriver(uint8_t addr)
  : _wire(&Wire), _addr(addr), _connected(false), _error(0)
{
}

// ============================================================
// begin — 检测芯片是否存在，启动时钟
// ============================================================
bool PCF8563TDriver::begin()
{
  Serial.printf("[PCF8563] Checking address 0x%02X on I2C0...\n", _addr);

  i2cBusLock();
  _wire->beginTransmission(_addr);
  _error = _wire->endTransmission();
  i2cBusUnlock();

  if (_error != 0)
  {
    Serial.printf("[PCF8563] ERROR: Device not found (I2C error %d)\n", _error);
    _connected = false;
    return false;
  }

  // 检查并清除 VL (Voltage Low) 标志
  uint8_t vlSec = 0;
  if (readRegisters(REG_VL_SEC, &vlSec, 1))
  {
    if (vlSec & 0x80)
    {
      Serial.println("[PCF8563] WARN: Voltage Low flag set! Time may be invalid.");
      // 清除 VL 标志 (写入秒值，保持原有秒，清除 bit7)
      uint8_t sec = vlSec & 0x7F;
      writeRegisters(REG_VL_SEC, &sec, 1);
    }
  }

  // 若时钟停止 (STOP bit)，则启动
  uint8_t ctrl1 = 0;
  if (readRegisters(REG_CTRL1, &ctrl1, 1))
  {
    if (ctrl1 & 0x20)  // STOP bit = 1 → 时钟已停止
    {
      ctrl1 &= ~0x20;  // 清除 STOP bit
      writeRegisters(REG_CTRL1, &ctrl1, 1);
      Serial.println("[PCF8563] Clock was stopped, started.");
    }
  }

  _connected = true;
  Serial.println("[PCF8563] Init OK");
  return true;
}

// ============================================================
// readTime — 读取当前时间
// ============================================================
bool PCF8563TDriver::readTime(DateTime &out)
{
  if (!_connected) return false;

  uint8_t buf[7];
  if (!readRegisters(REG_VL_SEC, buf, 7))
    return false;

  out.second  = bcd2dec(buf[0] & 0x7F);
  out.minute  = bcd2dec(buf[1] & 0x7F);
  out.hour    = bcd2dec(buf[2] & 0x3F);
  out.day     = bcd2dec(buf[3] & 0x3F);
  out.weekday = bcd2dec(buf[4] & 0x07);
  out.month   = bcd2dec(buf[5] & 0x1F);
  out.year    = 2000 + bcd2dec(buf[6]);

  return true;
}

// ============================================================
// writeTime — 写入时间
// ============================================================
bool PCF8563TDriver::writeTime(const DateTime &dt)
{
  if (!_connected) return false;

  uint8_t buf[7];
  buf[0] = dec2bcd(dt.second);                    // 秒 (VL bit = 0)
  buf[1] = dec2bcd(dt.minute);                    // 分
  buf[2] = dec2bcd(dt.hour);                      // 时
  buf[3] = dec2bcd(dt.day);                       // 日
  buf[4] = dec2bcd(dt.weekday);                   // 星期
  buf[5] = dec2bcd(dt.month);                     // 月 (世纪位 = 0)
  buf[6] = dec2bcd((uint8_t)(dt.year % 100));     // 年 (0-99)

  return writeRegisters(REG_VL_SEC, buf, 7);
}

// ============================================================
// isVoltageLow — 检查 VL 标志
// ============================================================
bool PCF8563TDriver::isVoltageLow()
{
  uint8_t vlSec = 0;
  if (readRegisters(REG_VL_SEC, &vlSec, 1))
    return (vlSec & 0x80) != 0;
  return false;
}

// ============================================================
// I2C 底层读写
// ============================================================
bool PCF8563TDriver::readRegisters(uint8_t startReg, uint8_t *data, uint8_t len)
{
  i2cBusLock();
  _wire->beginTransmission(_addr);
  _wire->write(startReg);
  _error = _wire->endTransmission();

  bool ok = (_error == 0);
  if (ok) {
    uint8_t count = _wire->requestFrom(_addr, len);
    ok = (count >= len);
    if (ok) {
      for (uint8_t i = 0; i < len; i++)
        data[i] = _wire->read();
    }
  }
  if (ok) i2cBusReportSuccess(); else i2cBusReportError();
  i2cBusUnlock();
  return ok;
}

bool PCF8563TDriver::writeRegisters(uint8_t startReg, const uint8_t *data, uint8_t len)
{
  i2cBusLock();
  _wire->beginTransmission(_addr);
  _wire->write(startReg);
  for (uint8_t i = 0; i < len; i++)
    _wire->write(data[i]);
  _error = _wire->endTransmission();

  bool ok = (_error == 0);
  if (ok) i2cBusReportSuccess(); else i2cBusReportError();
  i2cBusUnlock();
  return ok;
}

// ============================================================
// toEpoch — 将 DateTime 转为自 2000-01-01 00:00:00 的总秒数
// 便于计算两个时间点的差值 (不跨世纪)
// ============================================================
uint32_t PCF8563TDriver::toEpoch(const DateTime &dt)
{
  // 计算自 2000-01-01 起的天数
  int y = (int)dt.year;
  int m = (int)dt.month;
  int d = (int)dt.day;

  // 每月天数
  static const int daysInMon[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

  uint32_t totalDays = 0;

  // 整年
  for (int yy = 2000; yy < y; yy++) {
    bool leap = (yy % 4 == 0 && (yy % 100 != 0 || yy % 400 == 0));
    totalDays += leap ? 366 : 365;
  }

  // 整月 (当前年内)
  bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
  for (int mm = 1; mm < m; mm++) {
    int dim = daysInMon[mm - 1];
    if (mm == 2 && leap) dim = 29;
    totalDays += dim;
  }

  // 当月天数
  totalDays += (d - 1);

  // 转换为秒 + 时分
  uint32_t totalSec = totalDays * 86400UL
                    + (uint32_t)dt.hour * 3600UL
                    + (uint32_t)dt.minute * 60UL
                    + (uint32_t)dt.second;

  return totalSec;
}

// ============================================================
// BCD 编解码
// ============================================================
uint8_t PCF8563TDriver::dec2bcd(uint8_t val)
{
  return ((val / 10) << 4) | (val % 10);
}

uint8_t PCF8563TDriver::bcd2dec(uint8_t val)
{
  return (val >> 4) * 10 + (val & 0x0F);
}
