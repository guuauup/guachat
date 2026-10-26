/**
 * TCA9535 I2C IO 扩展器封装 — I2C0 总线（引脚见 bsp_pins.h）
 *
 * 引脚分配:
 *   P00 (TCA_P00) → 音量减按键     (主动低, 外部上拉)
 *   P01 (TCA_P01) → 音量加按键     (主动低, 外部上拉)
 *   GPS LDO EN: P02 (HIGH=开启)
 *   总电源开关: P03 (HIGH=运行 / LOW=关机)
 *   P10: 硬件版本脚 (10K 上拉), 固件只保持输入, 不驱动
 *   P14 (TCA_P14) → SHT30 RST       (HIGH=正常运行, LOW=复位, 10K上拉)
 *   P15 (TCA_P15) → 触摸屏 RST     (HIGH=正常, LOW=复位)
 *   P16 (TCA_P16) → 显示屏 RST     (HIGH=正常, LOW=复位)
 *
 * INT 引脚: 见 bsp_pins.h（下降沿中断）
 * P17: TF 卡座 CD (插入拉低)
 *
 * 使用 robtillaart/TCA9555 库的 TCA9535 派生类
 */
#ifndef TCA9535_MANAGER_H
#define TCA9535_MANAGER_H

// ════════════════════════════════════════════════════════════════
//  本工程适用的板卡:
//     GPS LDO EN = P02 (HIGH=开启)
//     总电源开关 = P03 (HIGH=运行 / LOW=关机)
//  P10 是硬件版本脚 (10K 上拉), 固件只保持输入, 不驱动。
// ════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include "TCA9555.h"

#include "bsp/bsp_pins.h"

class TCA9535Manager
{
public:
  static TCA9535Manager *getInstance();

  /** 初始化 Wire (I2C0) + TCA9535 芯片，配置 RST 引脚为输出并拉高 */
  bool begin();
  void enableInterrupt();  // 在调用者所在核心注册 INT 下降沿 ISR

  /** 硬件复位显示屏: P16 LOW 50ms → HIGH 120ms */
  void displayReset();

  /** 硬件复位触摸屏: P15 LOW 10ms → HIGH 50ms */
  void touchReset();

  /** 软件关机: 电源开关脚 P03 输出 LOW, 切断总电源 */
  void shutdown();

  /** 检查 INT 是否触发 (消费标志, 调用后自动清零) */
  bool hasInterrupt();

  /** 诊断: ISR 触发总次数（排查中断风暴） */
  uint32_t interruptCount() const { return _intCount; }

  /** 配置 TF 卡 CD 引脚 (P17 输入) */
  void configureCDPin();

  /** 读取 TF 卡 CD 状态 (LOW=已插入) */
  bool isCardInserted();

  /** 通用写引脚 (pin 0-15) */
  void writePin(uint8_t pin, bool level);

  /** 通用读引脚 (pin 0-15) */
  bool readPin(uint8_t pin);

  /** 一次性读取 P00/P01 两个输入（板级标注为音量减/加，低有效；单次 I2C 事务）
   *  非阻塞: 总线被占用(SHT30 时钟拉伸)时立即返回 false, 不阻塞调用方。
   *  @return true=已读到引脚状态; false=总线忙, 本次跳过 (保持上次状态) */
  bool readButtons(bool &p00, bool &p01);

  /// @name 引脚常量
  /// @{
  // 引脚定义唯一来源: src/bsp/bsp_pins.h
  static constexpr uint8_t PIN_VERSION_DETECT = BSP_TCA9535_P10;
  static constexpr uint8_t PIN_GPS_LDO_EN     = BSP_TCA9535_P02;
  static constexpr uint8_t PIN_POWER_SW       = BSP_TCA9535_P03;
  static constexpr uint8_t PIN_DISPLAY_RST    = BSP_TCA9535_P16;
  static constexpr uint8_t PIN_TOUCH_RST      = BSP_TCA9535_P15;
  static constexpr uint8_t PIN_SHT30_RST      = BSP_TCA9535_P14;
  static constexpr uint8_t PIN_TF_CD          = BSP_TCA9535_P17;
  static constexpr uint8_t PIN_VOL_DOWN       = BSP_TCA9535_P00;
  static constexpr uint8_t PIN_VOL_UP         = BSP_TCA9535_P01;
  static constexpr int     INT_PIN            = BSP_TCA9535_PIN_INT;
  /// @}

  /** 开机输出掩码: 本板全部 HIGH
   *  (电源 P03 HIGH=运行, GPS P02 HIGH=开启, 屏/触摸/SHT30 复位脚释放) */
  static constexpr uint16_t POWER_ON_MASK = 0xFFFF;

  /** GPS LDO 控制 (P02, HIGH=开启) */
  void setGpsLdo(bool on) { writePin(PIN_GPS_LDO_EN, on); }

  /** 设置 INT 通知的任务句柄（ISR 直接唤醒该任务） */
  void setNotifyHandle(TaskHandle_t h);

  /** ISR (IRAM, TCA9535 INT → 通知注册的任务) */
  static void IRAM_ATTR isrHandler();

private:
  TCA9535Manager();
  TCA9535Manager(const TCA9535Manager &) = delete;
  TCA9535Manager &operator=(const TCA9535Manager &) = delete;

  static TCA9535Manager *_instance;
  static volatile bool _intFlag;      // ISR 消费标志
  static volatile uint32_t _intCount; // ISR 触发总次数（中断风暴诊断）
  static TaskHandle_t _notifyHandle;   // 目标任务句柄 (ISR 直接唤醒)
  TCA9535 _tca;
  bool _ready;
};

#endif
