/**
 * TCA9535 I2C IO 扩展器封装实现
 *
 * I2C0 总线与芯片地址见 bsp_pins.h
 */
#include "TCA9535.h"
#include "I2CBus.h"
#include <Wire.h>
#include <freertos/task.h>

// --- 单例 & 静态成员 ---
TCA9535Manager *TCA9535Manager::_instance = nullptr;
volatile bool TCA9535Manager::_intFlag = false;
volatile uint32_t TCA9535Manager::_intCount = 0;
TaskHandle_t TCA9535Manager::_notifyHandle = nullptr;

TCA9535Manager::TCA9535Manager()
  : _tca(BSP_I2C_ADDR_TCA9535, &Wire), _ready(false)
{
}

TCA9535Manager *TCA9535Manager::getInstance()
{
  if (!_instance) _instance = new TCA9535Manager();
  return _instance;
}

// ============================================================
// begin — 初始化 I2C0 总线 + TCA9535 芯片
// ============================================================
bool TCA9535Manager::begin()
{
  Serial.println("[TCA9535] Initializing I2C0...");

  // 0) I2C 总线锁 + 开机恢复 (崩溃后 SDA 可能被从机拉死, 必须先抢救再初始化)
  i2cBusInit();
  i2cBusRecoverNow();   // 内部完成 Wire.begin() + setClock(100kHz)
  Serial.printf("[TCA9535] Wire begin OK (SCL=%d, SDA=%d, 100kHz)\n",
                I2C0_SCL_PIN, I2C0_SDA_PIN);

  if (!_tca.isConnected())
  {
    Serial.println("[TCA9535] ERROR: Device not found on I2C0 bus!");
    _ready = false;
    return false;
  }

  // 2) 本板 GPIO 分配: GPS LDO EN=P02, 总电源开关=P03 (HIGH=运行/LOW=关机)。
  //    P10 是硬件版本脚(10K 上拉), 固件不驱动, 下面 pinMode16 只把它留作输入。
  Serial.println("[TCA9535] 板卡: GPS=P02, 电源=P03");

  // 3) 初始化芯片为输出 (先写电平再切方向, 全程无毛刺)
  //    POWER_ON_MASK=0xFFFF: 电源 P03 HIGH=运行, GPS P02 HIGH=开启,
  //    P14/P15/P16 HIGH=释放 SHT30/触摸/屏幕复位
  //
  //    ⚠️ 不能用库的 begin(OUTPUT, mask) —— 它内部先 pinMode16(0x0000) 切成输出、
  //    再 write16(mask) 写电平, 中间会短暂输出上电默认值。本板电源脚高有效,
  //    若默认 LOW 会把 P03 拉低 → AO3401 导通 → GEK100 RST 拉高 → 直接断电。
  //    所以"先写输出寄存器、再切方向", 引脚从高阻直接变目标电平, 无 LOW 毛刺。
  _tca.write16(POWER_ON_MASK);   // ① 先写输出寄存器 (引脚仍是输入态, 不影响实际电平)
  // ② 再切方向 → 立即输出 POWER_ON_MASK (pinMode 掩码位=1 表示输入)
  //    只把 P10(版本脚) 留在输入, 其余全部输出
  _tca.pinMode16((uint16_t)(1u << PIN_VERSION_DETECT));

  // 4) 验证芯片类型 (TCA9535 返回 35)
  uint8_t type = _tca.getType();
  Serial.printf("[TCA9535] Device connected, type: %d (expected 35)\n", type);

  // 5) 引脚已在上面 write16(POWER_ON_MASK)+pinMode16(仅 P10 输入) 中设好
  //    (电源开关=运行电平, 其余输出 HIGH), 无需额外 write1, 直接标记就绪

  // 6) 配置 P17 为输入 (TF 卡 CD)
  _tca.pinMode1(PIN_TF_CD, INPUT);

  // 7) 配置 P00/P01 为输入 (音量加减按键, 主动低, 外部上拉)
  _tca.pinMode1(PIN_VOL_DOWN, INPUT);
  _tca.pinMode1(PIN_VOL_UP, INPUT);

  // ISR 注册延后到 Core 1 (enableInterrupt)
  _ready = true;
  Serial.println("[TCA9535] Init complete");
  return true;
}

// ============================================================
// enableInterrupt — 注册 INT 下降沿 ISR
// ============================================================
void TCA9535Manager::enableInterrupt()
{
  pinMode(INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INT_PIN), isrHandler, FALLING);
  Serial.printf("[TCA9535] INT pin configured on GPIO%d\n", INT_PIN);
}

// ============================================================
// displayReset — 硬件复位显示屏 (P16)
// ============================================================
void TCA9535Manager::displayReset()
{
  if (!_ready)
  {
    Serial.println("[TCA9535] WARN: displayReset called but device not ready");
    return;
  }
  Serial.println("[TCA9535] Display reset: P16 LOW");
  _tca.write1(PIN_DISPLAY_RST, LOW);
  delay(50);
  Serial.println("[TCA9535] Display reset: P16 HIGH");
  _tca.write1(PIN_DISPLAY_RST, HIGH);
  delay(120);
  Serial.println("[TCA9535] Display reset done");
}

// ============================================================
// touchReset — 硬件复位触摸屏 (P15)
// ============================================================
void TCA9535Manager::touchReset()
{
  if (!_ready)
  {
    Serial.println("[TCA9535] WARN: touchReset called but device not ready");
    return;
  }
  Serial.println("[TCA9535] Touch reset: P15 LOW");
  _tca.write1(PIN_TOUCH_RST, LOW);
  delay(10);
  Serial.println("[TCA9535] Touch reset: P15 HIGH");
  _tca.write1(PIN_TOUCH_RST, HIGH);
  delay(200);  // FT6336U 复位后需 ~100ms+ 才 ready, 50ms 太短会读到 chip_id=0x00
  Serial.println("[TCA9535] Touch reset done");
}

// ============================================================
// shutdown — 软件关机 (电源开关 P03 输出 LOW)
// ============================================================
void TCA9535Manager::shutdown()
{
  if (!_ready)
  {
    Serial.println("[TCA9535] WARN: shutdown called but device not ready");
    return;
  }
  Serial.printf("[TCA9535] Software shutdown: P%d LOW\n", PIN_POWER_SW);
  i2cBusLock();
  _tca.write1(PIN_POWER_SW, LOW);
  i2cBusUnlock();
  // 此后总电源切断，ESP32 掉电
}

// ============================================================
// setNotifyHandle — 设置 ISR 唤醒的目标任务
// ============================================================
void TCA9535Manager::setNotifyHandle(TaskHandle_t h)
{
  _notifyHandle = h;
}

// ============================================================
// ISR — TCA9535 INT 引脚下降沿
// 直接唤醒注册的任务（例如 CD 插拔检测）
// ============================================================
void IRAM_ATTR TCA9535Manager::isrHandler()
{
  _intCount++;
  _intFlag = true;
  if (_notifyHandle) {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(_notifyHandle, &woken);
    if (woken == pdTRUE) portYIELD_FROM_ISR();
  }
}

// ============================================================
// hasInterrupt — 检查 INT 是否触发 (消费标志)
// ============================================================
bool TCA9535Manager::hasInterrupt()
{
  bool flag = _intFlag;
  _intFlag = false;
  return flag;
}

// ============================================================
// TF 卡 CD 引脚 (P17, 插入拉低)
// ============================================================
void TCA9535Manager::configureCDPin()
{
  if (!_ready) return;
  i2cBusLock();
  _tca.pinMode1(PIN_TF_CD, INPUT);
  i2cBusUnlock();
}

bool TCA9535Manager::isCardInserted()
{
  if (!_ready) return false;
  i2cBusLock();
  bool v = (_tca.read1(PIN_TF_CD) == LOW);
  i2cBusUnlock();
  return v;
}

// ============================================================
// 通用引脚读写
// ============================================================
void TCA9535Manager::writePin(uint8_t pin, bool level)
{
  if (_ready) {
    i2cBusLock();
    _tca.write1(pin, level ? HIGH : LOW);
    i2cBusUnlock();
  }
}

bool TCA9535Manager::readPin(uint8_t pin)
{
  if (!_ready) return false;
  i2cBusLock();
  bool v = (_tca.read1(pin) == HIGH);
  i2cBusUnlock();
  return v;
}

bool TCA9535Manager::readButtons(bool &p00, bool &p01)
{
  p00 = p01 = false;
  if (!_ready) return false;

  // 非阻塞加锁: 总线正被 SHT30 15ms 时钟拉伸占用时立即返回,
  // 避免在 UI/实时路径里阻塞渲染。
  if (!i2cBusTryLock()) return false;

  // 两个输入都在 port0 (P00=bit0, P01=bit1), 单次 I2C 读 port0 取出
  int port = _tca.read8(0);
  i2cBusUnlock();

  p00 = ((port & (1 << PIN_VOL_DOWN)) == 0);  // 主动低: 0=按下
  p01 = ((port & (1 << PIN_VOL_UP))   == 0);
  return true;
}
