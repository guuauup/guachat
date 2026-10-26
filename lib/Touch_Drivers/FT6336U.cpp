/**************************************************************************/
/*!
 @file     FT6336U.cpp
 @author   Atsushi Sasaki (https://github.com/aselectroworks)
 @license  MIT
 Modified for ESP32-S3 with LVGL integration
*/
/**************************************************************************/

#include "FT6336U.h"

// ============================================================
// ISR 桥接: 用普通函数指针取代 FunctionalInterrupt 的 std::function,
// 避免 ISR 内间接调用 std::function (可能位于 flash, 且 std::bind 对象在堆上)。
// ============================================================
static FT6336U* s_ft6336u = nullptr;
static void IRAM_ATTR ft6336u_isr(void)
{
  if (s_ft6336u) s_ft6336u->handleISR();
}

// ============================================================
// Constructor & Begin
// ============================================================

FT6336U::FT6336U(int SDAPin, int SCLPin, int INTPin, TwoWire *wirePort)
{
  _deviceAddress = I2C_ADDR_FT6336U;
  _SDAPin = uint8_t(SDAPin);
  _SCLPin = uint8_t(SCLPin);
  _INTPin = uint8_t(INTPin);
  _i2cPort = wirePort;
  _event_available = false;
  _intCount = 0;
}

bool FT6336U::begin(void)
{
  _i2cPort->begin(_SDAPin, _SCLPin);
  _i2cPort->setClock(400000);  // 400kHz 快速模式

  // 尝试读取，FT6336U 的 DEVICE_MODE 寄存器在 working mode 下返回 0x00 或 0x02
  uint8_t dev_type = read_device_type();
  Serial.printf("[FT6336U] I2C scan: dev_type=0x%02X, chip_id=0x%02X\n",
                dev_type, read_chip_id());

  // 写入 TD_STATUS 寄存器解锁触摸检测
  writeByte(0x02, 0x01);

  // 尝试注册中断（下降沿）—— 用普通函数指针, 避开 FunctionalInterrupt 的 std::function
  s_ft6336u = this;
  pinMode(_INTPin, INPUT_PULLUP);
  delay(10);   // 解锁后触摸芯片可能立即拉低 INT 扫描一次, 先让其稳定再挂中断
  _intCount = 0;   // 诊断: 清零后再挂中断, 便于观察挂上后是否立即风暴
  attachInterrupt(_INTPin, ft6336u_isr, FALLING);

  // 诊断: 挂上中断后立刻采样计数, 若几十 ms 内狂涨即 GPIO45 中断风暴
  delay(50);
  Serial.printf("[Touch] INT count=%lu @+50ms\n", (unsigned long)_intCount);
  delay(100);
  Serial.printf("[Touch] INT count=%lu @+150ms\n", (unsigned long)_intCount);

  return (dev_type == 0x00 || dev_type == 0x02);
}

// ============================================================
// ISR & Available (for LVGL polling)
// ============================================================

void IRAM_ATTR FT6336U::handleISR(void)
{
  _intCount++;
  _event_available = true;
}

bool FT6336U::available(void)
{
  // 方法1: 中断触发 (INT 引脚下降沿)
  if (_event_available)
  {
    scan();
    _event_available = false;
    return (touchPoint.touch_count > 0);
  }

  // 方法2: 轮询兜底 (INT 中断不工作时也能用)
  uint8_t td = read_td_status();
  if (td & 0x0F)
  {
    scan();
    return true;
  }
  return false;
}

// ============================================================
// I2C Low Level
// ============================================================

uint8_t FT6336U::readByte(uint8_t addr)
{
  uint8_t rdData = 0;
  uint8_t rdDataCount;
  uint8_t retries = 10;   // 有界重试: 总线异常时避免死循环卡死 UI 任务

  do {
    _i2cPort->beginTransmission(_deviceAddress);
    _i2cPort->write(addr);
    _i2cPort->endTransmission(false); // Restart
    rdDataCount = _i2cPort->requestFrom(_deviceAddress, (uint8_t)1);
    if (rdDataCount == 0) delay(1);   // 让出 CPU + 给总线恢复时间
  } while (rdDataCount == 0 && --retries > 0);

  if (_i2cPort->available())
  {
    rdData = _i2cPort->read();
  }
  return rdData;
}

void FT6336U::writeByte(uint8_t addr, uint8_t data)
{
  _i2cPort->beginTransmission(_deviceAddress);
  _i2cPort->write(addr);
  _i2cPort->write(data);
  _i2cPort->endTransmission();
}

// ============================================================
// Device Info
// ============================================================

uint8_t FT6336U::read_device_type(void)
{
  return readByte(FT6336U_ADDR_DEVICE_MODE);
}

uint8_t FT6336U::read_device_mode(void)
{
  return (readByte(FT6336U_ADDR_DEVICE_MODE) & 0x70) >> 4;
}

void FT6336U::write_device_mode(DEVICE_MODE_Enum mode)
{
  uint8_t buf = readByte(FT6336U_ADDR_DEVICE_MODE);
  buf = (buf & 0x8F) | (mode << 4);
  writeByte(FT6336U_ADDR_DEVICE_MODE, buf);
}

uint8_t FT6336U::read_gesture_id(void)
{
  return readByte(FT6336U_ADDR_GESTURE_ID);
}

uint8_t FT6336U::read_td_status(void)
{
  return readByte(FT6336U_ADDR_TD_STATUS);
}

uint8_t FT6336U::read_touch_number(void)
{
  return (readByte(FT6336U_ADDR_TD_STATUS) & 0x0F);
}

// ============================================================
// Touch Point 1
// ============================================================

uint16_t FT6336U::read_touch1_x(void)
{
  uint8_t xh = readByte(0x03);  // P1_XH: bits 3-0 = X[11:8]
  uint8_t xl = readByte(0x04);  // P1_XL: X[7:0]
  return (((uint16_t)(xh & 0x0F)) << 8) | xl;
}

uint16_t FT6336U::read_touch1_y(void)
{
  uint8_t yh = readByte(0x05);  // P1_YH: bits 3-0 = Y[11:8]
  uint8_t yl = readByte(0x06);  // P1_YL: Y[7:0]
  return (((uint16_t)(yh & 0x0F)) << 8) | yl;
}

uint8_t FT6336U::read_touch1_event(void)
{
  return (readByte(FT6336U_ADDR_TOUCH1_EVENT) & 0xC0) >> 6;
}

uint8_t FT6336U::read_touch1_id(void)
{
  return readByte(FT6336U_ADDR_TOUCH1_ID);
}

uint8_t FT6336U::read_touch1_weight(void)
{
  return readByte(FT6336U_ADDR_TOUCH1_WEIGHT);
}

uint8_t FT6336U::read_touch1_misc(void)
{
  return readByte(FT6336U_ADDR_TOUCH1_MISC);
}

// ============================================================
// Touch Point 2
// ============================================================

uint16_t FT6336U::read_touch2_x(void)
{
  uint8_t xh = readByte(0x09);  // P2_XH
  uint8_t xl = readByte(0x0A);  // P2_XL
  return (((uint16_t)(xh & 0x0F)) << 8) | xl;
}

uint16_t FT6336U::read_touch2_y(void)
{
  uint8_t yh = readByte(0x0B);  // P2_YH
  uint8_t yl = readByte(0x0C);  // P2_YL
  return (((uint16_t)(yh & 0x0F)) << 8) | yl;
}

uint8_t FT6336U::read_touch2_event(void)
{
  return (readByte(FT6336U_ADDR_TOUCH2_EVENT) & 0xC0) >> 6;
}

uint8_t FT6336U::read_touch2_id(void)
{
  return readByte(FT6336U_ADDR_TOUCH2_ID);
}

uint8_t FT6336U::read_touch2_weight(void)
{
  return readByte(FT6336U_ADDR_TOUCH2_WEIGHT);
}

uint8_t FT6336U::read_touch2_misc(void)
{
  return readByte(FT6336U_ADDR_TOUCH2_MISC);
}

// ============================================================
// Mode Parameter Register
// ============================================================

void FT6336U::write_power_mode(uint8_t mode)
{
  writeByte(FT6336U_ADDR_POWER_MODE, mode);
}

void FT6336U::write_monitor_mode_period(uint8_t mode)
{
  writeByte(FT6336U_ADDR_TIME_ENTER_MONITOR, mode);
}

void FT6336U::disable_face_dec_mode(void)
{
  writeByte(FT6336U_ADDR_CTRL, 0x00);
}

uint8_t FT6336U::read_touch_threshold(void)
{
  return readByte(FT6336U_ADDR_THRESHOLD);
}

uint8_t FT6336U::read_filter_coefficient(void)
{
  return readByte(FT6336U_ADDR_FILTER_COE);
}

uint8_t FT6336U::read_ctrl_mode(void)
{
  return readByte(FT6336U_ADDR_CTRL);
}

void FT6336U::write_ctrl_mode(CTRL_MODE_Enum mode)
{
  writeByte(FT6336U_ADDR_CTRL, mode);
}

uint8_t FT6336U::read_time_period_enter_monitor(void)
{
  return readByte(FT6336U_ADDR_TIME_ENTER_MONITOR);
}

void FT6336U::write_time_period_enter_monitor(uint8_t time)
{
  writeByte(FT6336U_ADDR_TIME_ENTER_MONITOR, time);
}

uint8_t FT6336U::read_active_rate(void)
{
  return readByte(FT6336U_ADDR_ACTIVE_MODE_RATE);
}

uint8_t FT6336U::read_monitor_rate(void)
{
  return readByte(FT6336U_ADDR_MONITOR_MODE_RATE);
}

// ============================================================
// Gesture Parameter Register
// ============================================================

uint8_t FT6336U::read_radian_value(void)
{
  return readByte(FT6336U_ADDR_RADIAN_VALUE);
}

void FT6336U::write_radian_value(uint8_t val)
{
  writeByte(FT6336U_ADDR_RADIAN_VALUE, val);
}

uint8_t FT6336U::read_offset_left_right(void)
{
  return readByte(FT6336U_ADDR_OFFSET_LEFT_RIGHT);
}

void FT6336U::write_offset_left_right(uint8_t val)
{
  writeByte(FT6336U_ADDR_OFFSET_LEFT_RIGHT, val);
}

uint8_t FT6336U::read_offset_up_down(void)
{
  return readByte(FT6336U_ADDR_OFFSET_UP_DOWN);
}

void FT6336U::write_offset_up_down(uint8_t val)
{
  writeByte(FT6336U_ADDR_OFFSET_UP_DOWN, val);
}

uint8_t FT6336U::read_distance_left_right(void)
{
  return readByte(FT6336U_ADDR_DISTANCE_LEFT_RIGHT);
}

void FT6336U::write_distance_left_right(uint8_t val)
{
  writeByte(FT6336U_ADDR_DISTANCE_LEFT_RIGHT, val);
}

uint8_t FT6336U::read_distance_up_down(void)
{
  return readByte(FT6336U_ADDR_DISTANCE_UP_DOWN);
}

void FT6336U::write_distance_up_down(uint8_t val)
{
  writeByte(FT6336U_ADDR_DISTANCE_UP_DOWN, val);
}

uint8_t FT6336U::read_distance_zoom(void)
{
  return readByte(FT6336U_ADDR_DISTANCE_ZOOM);
}

void FT6336U::write_distance_zoom(uint8_t val)
{
  writeByte(FT6336U_ADDR_DISTANCE_ZOOM, val);
}

// ============================================================
// System Information
// ============================================================

uint16_t FT6336U::read_library_version(void)
{
  uint8_t buf[2];
  buf[0] = readByte(FT6336U_ADDR_LIBRARY_VERSION_H);
  buf[1] = readByte(FT6336U_ADDR_LIBRARY_VERSION_L);
  return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

uint8_t FT6336U::read_chip_id(void)
{
  return readByte(FT6336U_ADDR_CHIP_ID);
}

uint8_t FT6336U::read_g_mode(void)
{
  return readByte(FT6336U_ADDR_G_MODE);
}

void FT6336U::write_g_mode(G_MODE_Enum mode)
{
  writeByte(FT6336U_ADDR_G_MODE, mode);
}

uint8_t FT6336U::read_pwrmode(void)
{
  return readByte(FT6336U_ADDR_POWER_MODE);
}

uint8_t FT6336U::read_firmware_id(void)
{
  return readByte(FT6336U_ADDR_FIRMARE_ID);
}

uint8_t FT6336U::read_focaltech_id(void)
{
  return readByte(FT6336U_ADDR_FOCALTECH_ID);
}

uint8_t FT6336U::read_release_code_id(void)
{
  return readByte(FT6336U_ADDR_RELEASE_CODE_ID);
}

uint8_t FT6336U::read_state(void)
{
  return readByte(FT6336U_ADDR_STATE);
}

// ============================================================
// Scan - Read all touch points (burst read for speed)
// ============================================================

FT6336U_TouchPointType FT6336U::scan(void)
{
  // 突发读取 5 字节: 0x02(TD_STATUS) ~ 0x06(P1_YL)
  uint8_t buf[5] = {0};
  uint8_t rdCount;
  uint8_t retries = 10;   // 有界重试, 避免 I2C 异常时死循环
  do {
    _i2cPort->beginTransmission(_deviceAddress);
    _i2cPort->write(0x02);
    _i2cPort->endTransmission(false);
    rdCount = _i2cPort->requestFrom(_deviceAddress, (uint8_t)5);
    if (rdCount < 5) delay(1);
  } while (rdCount < 5 && --retries > 0);

  for (int i = 0; i < 5; i++) buf[i] = _i2cPort->read();

  touchPoint.touch_count = buf[0] & 0x0F;

  if (touchPoint.touch_count >= 1)
  {
    touchPoint.tp[0].x      = (((uint16_t)(buf[1] & 0x0F)) << 8) | buf[2];
    touchPoint.tp[0].y      = (((uint16_t)(buf[3] & 0x0F)) << 8) | buf[4];
    touchPoint.tp[0].status = (TouchStatusEnum)((buf[1] & 0xC0) >> 6);
  }
  else
  {
    touchPoint.tp[0].x = 0;
    touchPoint.tp[0].y = 0;
    touchPoint.tp[0].status = release;
  }

  if (touchPoint.touch_count >= 2)
  {
    // 突发读取 4 字节: 0x09 ~ 0x0C (P2)
    retries = 10;
    do {
      _i2cPort->beginTransmission(_deviceAddress);
      _i2cPort->write(0x09);
      _i2cPort->endTransmission(false);
      rdCount = _i2cPort->requestFrom(_deviceAddress, (uint8_t)4);
      if (rdCount < 4) delay(1);
    } while (rdCount < 4 && --retries > 0);

    uint8_t buf2[4] = {0};
    for (int i = 0; i < 4; i++) buf2[i] = _i2cPort->read();

    touchPoint.tp[1].x      = (((uint16_t)(buf2[0] & 0x0F)) << 8) | buf2[1];
    touchPoint.tp[1].y      = (((uint16_t)(buf2[2] & 0x0F)) << 8) | buf2[3];
    touchPoint.tp[1].status = (TouchStatusEnum)((buf2[0] & 0xC0) >> 6);
  }
  else
  {
    touchPoint.tp[1].x = 0;
    touchPoint.tp[1].y = 0;
    touchPoint.tp[1].status = release;
  }

  return touchPoint;
}