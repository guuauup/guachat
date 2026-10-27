/**
 * DRV2605L 线性马达驱动 — I2C0 (地址见 BSP_I2C_ADDR_DRV2605L)
 *
 * 板载 X 轴 LRA。EN 引脚上拉到 VDD，空闲时必须写 MODE.STANDBY=1
 * 才能进入低功耗。直接操作 Wire，不依赖 Adafruit 库。
 *
 * 上电流程：探测 → LRA 控制参数 → 读取/执行自动校准 → 强制谐振周期
 * → 回到 standby。校准结果保存在 NVS 命名空间 "drv2605"。
 */
#ifndef DRV2605L_DRIVER_H
#define DRV2605L_DRIVER_H

#include <Arduino.h>

#include "bsp/bsp_pins.h"

class DRV2605LDriver
{
public:
  explicit DRV2605LDriver(uint8_t addr = BSP_I2C_ADDR_DRV2605L);

  /** 初始化并校准；成功后芯片处于 standby。 */
  bool begin();

  bool isReady() const { return _ready; }

  /**
   * 重新执行 LRA 自动校准（阻塞 1–3 秒，马达会嗡鸣）。
   * 成功后覆盖 NVS 中的 comp/emp/fb；必须在后台任务里调用，不要放在 UI 回调。
   */
  bool recalibrate();

  /**
   * 播放一个 ROM 效果（Library 6 / LRA），立即返回。
   * rated_reg 为播放期间使用的 RATEDV 值。
   */
  bool play(uint8_t effect, uint8_t rated_reg);

  /**
   * 周期调用：GO 清零或超时后恢复空闲电压并进入 standby。
   * 返回 true 表示本次调用结束了播放。
   */
  bool poll();

  /** 立即停止并进入 standby。 */
  void stop();

private:
  bool readReg(uint8_t reg, uint8_t &value);
  bool writeReg(uint8_t reg, uint8_t value);

  bool configure();
  bool loadCalibration();
  bool calibrate();
  bool runCalibration(uint8_t &status, uint8_t &comp, uint8_t &emp,
                      uint8_t &period, uint8_t &feedback);
  bool saveCalibration(uint8_t comp, uint8_t emp, uint8_t period,
                       uint8_t feedback);

  bool exitStandby();
  void enterIdle();

  uint8_t _addr;
  bool    _ready;
  bool    _standby;
  bool    _playing;
  uint32_t _deadline_ms;

  // 寄存器地址
  static constexpr uint8_t REG_STATUS      = 0x00;
  static constexpr uint8_t REG_MODE        = 0x01;
  static constexpr uint8_t REG_LIBRARY     = 0x03;
  static constexpr uint8_t REG_WAVESEQ1    = 0x04;
  static constexpr uint8_t REG_GO          = 0x0C;
  static constexpr uint8_t REG_OVERDRIVE   = 0x0D;
  static constexpr uint8_t REG_SUSTAINPOS  = 0x0E;
  static constexpr uint8_t REG_SUSTAINNEG  = 0x0F;
  static constexpr uint8_t REG_BREAK       = 0x10;
  static constexpr uint8_t REG_RATEDV      = 0x16;
  static constexpr uint8_t REG_CLAMPV      = 0x17;
  static constexpr uint8_t REG_AUTOCALCOMP = 0x18;
  static constexpr uint8_t REG_AUTOCALEMP  = 0x19;
  static constexpr uint8_t REG_FEEDBACK    = 0x1A;
  static constexpr uint8_t REG_CONTROL1    = 0x1B;
  static constexpr uint8_t REG_CONTROL2    = 0x1C;
  static constexpr uint8_t REG_CONTROL3    = 0x1D;
  static constexpr uint8_t REG_CONTROL4    = 0x1E;
  static constexpr uint8_t REG_LRARESON    = 0x22;

  // MODE 寄存器
  static constexpr uint8_t MODE_INTTRIG  = 0x00;
  static constexpr uint8_t MODE_AUTOCAL  = 0x07;
  static constexpr uint8_t MODE_STANDBY  = 0x40;
};

#endif /* DRV2605L_DRIVER_H */
