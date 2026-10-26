/**
 * SX1280Radio.cpp — SX1280 (E28-2G4M20S) RadioLib 封装实现
 * 频率边界与模块级功率硬件上限见 RadioConfig.h；
 * 高于推荐功率只打印提示，不阻止用户。
 */

#include "SX1280Radio.h"

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <math.h>
#include "RadioConfig.h"

namespace {

// 独立 SPI 总线: 屏幕 TFT_eSPI 用全局 SPI 对象 (ESP32-S3 上默认 = FSPI = SPI2),
// LoRa 单独建一个 SPIClass 挂到 HSPI(=SPI3) 与之真正隔离, 消除 SPI2 被重复初始化
// 导致的 addApbChangeCallback duplicate 告警。GPIO 矩阵把 SPI3 的
// CLK/MOSI/MISO 路由到 GPIO6/5/4, 无需改 PCB。
SPIClass spiLoRa(HSPI);

// Module(cs, irq, rst, gpio, spi): NSS=7, DIO1=8, NRESET=17, BUSY=18
Module modLoRa(lora::PIN_NSS, lora::PIN_DIO1, lora::PIN_NRESET, lora::PIN_BUSY, spiLoRa);

// 暴露受保护的 setPacketType, 供频谱扫描在 LoRa/GFSK 间切换 (避免整机 reset 重初始化)。
class ScanRadio : public SX1280 {
public:
  using SX1280::SX1280;
  int16_t switchPacketType(uint8_t t) { return setPacketType(t); }
};

ScanRadio radio(&modLoRa);

// 功率策略：不做 7 dBm 硬限制，只提示。最高值由模块 20 dBm 硬件额定决定。
void notifyPowerRequest(int8_t requested, int8_t applied)
{
  if (requested > lora::TX_POWER_MAX_DBM) {
    Serial.printf("[LoRa] 请求 %d dBm 超过模块硬件上限，已限制为 %d dBm\n",
                  requested, applied);
  } else if (applied > lora::TX_POWER_RECOMMENDED_DBM) {
    Serial.printf("[LoRa] 提示: %d dBm 高于推荐值 %d dBm；与 WiFi/BLE 并发时"
                  "近距离可能损坏 ESP32-S3 LNA，请自行评估风险\n",
                  applied, lora::TX_POWER_RECOMMENDED_DBM);
  }
}

} // namespace

SX1280Radio::SX1280Radio()
  : _begun(false), _powerDbm(lora::DEFAULT_POWER_DBM)
{}

// 模块级辐射 dBm → SX1280 芯片级 PA dBm (RadioLib setOutputPower 范围 -18..+13)
// 先夹紧模块级功率, 保证任何调用路径都无法越过上限。
int8_t SX1280Radio::toChipDbm(int8_t moduleDbm) const
{
  int8_t clamped = lora::clampTxPower(moduleDbm);
  int8_t chip = clamped - lora::PA_GAIN_M20S;
  if (chip < lora::CHIP_MIN_DBM) chip = lora::CHIP_MIN_DBM;
  if (chip > lora::CHIP_MAX_DBM) chip = lora::CHIP_MAX_DBM;
  return chip;
}

bool SX1280Radio::begin()
{
  if (_begun) return true;

  // 独立 SPI 总线初始化: begin(sck, miso, mosi, ss)
  spiLoRa.begin(lora::PIN_SCK, lora::PIN_MISO, lora::PIN_MOSI, lora::PIN_NSS);

  // E28 外置 PA 收发开关: 必须设置, 否则 PA 不导通
  radio.setRfSwitchPins(lora::PIN_RXEN, lora::PIN_TXEN);

  // 默认参数：2405 MHz / SF10 / BW812.5 / CR4/5 / 前导 8
  int16_t s = radio.begin(lora::DEFAULT_FREQ_MHZ,
                          lora::DEFAULT_BW_KHZ,
                          lora::DEFAULT_SF,
                          lora::LORA_CR,
                          lora::LORA_SYNC_WORD,
                          toChipDbm(_powerDbm),
                          lora::LORA_PREAMBLE);
  if (s != RADIOLIB_ERR_NONE) {
    Serial.printf("[LoRa] begin 失败, 错误码 %d\n", s);
    return false;
  }

  _begun = true;
  Serial.printf("[LoRa] 初始化成功 %.1fMHz SF%u BW%.3fkHz CR4/5 功率%ddBm\n",
                lora::DEFAULT_FREQ_MHZ, lora::DEFAULT_SF,
                lora::DEFAULT_BW_KHZ, _powerDbm);
  return true;
}

bool SX1280Radio::setPower(int8_t dBm)
{
  if (!_begun) return false;
  _powerDbm = lora::clampTxPower(dBm);
  notifyPowerRequest(dBm, _powerDbm);
  int16_t s = radio.setOutputPower(toChipDbm(_powerDbm));
  return (s == RADIOLIB_ERR_NONE);
}

bool SX1280Radio::applyParams(float freqMHz, uint8_t sf, float bwKhz, int8_t powerDbm)
{
  if (!_begun) return false;
  const int8_t requestedPower = powerDbm;
  freqMHz    = lora::clampFreqFloat(freqMHz);
  powerDbm   = lora::clampTxPower(powerDbm);
  _powerDbm  = powerDbm;
  notifyPowerRequest(requestedPower, powerDbm);
  radio.standby();
  int16_t s1 = radio.setFrequency(freqMHz);
  int16_t s2 = radio.setSpreadingFactor(sf);
  int16_t s3 = radio.setBandwidth(bwKhz);
  int16_t s4 = radio.setOutputPower(toChipDbm(powerDbm));
  if (s1 == RADIOLIB_ERR_NONE && s2 == RADIOLIB_ERR_NONE &&
      s3 == RADIOLIB_ERR_NONE && s4 == RADIOLIB_ERR_NONE) {
    return true;
  }
  return false;
}

int16_t SX1280Radio::startCAD()
{
  if (!_begun) return RADIOLIB_ERR_UNKNOWN;
  // scanChannel() = 阻塞式 CAD, 返回 RADIOLIB_CHANNEL_FREE / DETECTED
  return radio.scanChannel();
}

int16_t SX1280Radio::scanRssiAt(float freqMHz, uint16_t samples, uint32_t sampleUs,
                                int8_t busyThresholdDbm, ScanSample* out)
{
  if (!_begun) return RADIOLIB_ERR_UNKNOWN;
  if (samples == 0) samples = 1;

  // SX1280 切频率须在 standby 下进行, 之后进连续接收才能读到有效瞬时 RSSI
  radio.standby();
  int16_t s = radio.setFrequency(freqMHz);
  if (s != RADIOLIB_ERR_NONE) return s;
  s = radio.startReceive();
  if (s != RADIOLIB_ERR_NONE) return s;

  delayMicroseconds(800);   // RX 建立 + 本振稳定

  int32_t  sum  = 0;
  int8_t   peak = -128;
  int8_t   mn   = 127;
  uint16_t busy = 0;
  for (uint16_t i = 0; i < samples; i++) {
    int8_t r = (int8_t)lroundf(radio.getRSSI(false));   // false = 瞬时 RSSI (GetRssiInst)
    sum += r;
    if (r > peak) peak = r;
    if (r < mn)   mn   = r;
    if (r > busyThresholdDbm) busy++;
    // 每 32 样本让出一次 CPU 喂 idle/task_wdt (Core0 长扫描不饿死 idle 触发 TWDT)
    if ((i & 31) == 31) vTaskDelay(pdMS_TO_TICKS(1));
    else delayMicroseconds(sampleUs);
  }

  if (out) {
    out->meanDbm      = (int8_t)lroundf((float)sum / samples);
    out->peakDbm      = peak;
    out->minDbm       = mn;
    out->occupancyPct = (uint8_t)((uint32_t)busy * 100 / samples);
  }
  return RADIOLIB_ERR_NONE;
}

int16_t SX1280Radio::enterScanMode()
{
  if (!_begun) return RADIOLIB_ERR_UNKNOWN;

  // LoRa 模式的 GetRssiInst (getRSSI(false)) 是 chirp 基带值, 对宽带 OFDM(WiFi) 读不准,
  // 贴脸路由器也封顶 ~-70。切 GFSK 后它才是平坦能量检测器, 能读出真实信道能量。
  // 注意: GFSK 带宽由 setBitRate 决定 (bitrate/BW 耦合, 见 RadioLib setBitRate),
  // 800kbps → 2.4MHz 信道滤波 (GFSK 最宽), 不能用 setBandwidth (LoRa/Ranging 专用)。
  radio.standby();
  int16_t s = radio.switchPacketType(RADIOLIB_SX128X_PACKET_TYPE_GFSK);
  if (s != RADIOLIB_ERR_NONE) return s;
  s = radio.setBitRate(800.0f);               // → GFSK BW 2.4MHz
  if (s != RADIOLIB_ERR_NONE) return s;
  s = radio.setFrequencyDeviation(400.0f);    // modIndex = 8*400/800-1 = 3 (合法)
  if (s != RADIOLIB_ERR_NONE) return s;
  s = radio.setDataShaping(RADIOLIB_SHAPING_0_5);
  if (s != RADIOLIB_ERR_NONE) return s;
  // 高灵敏度模式: 打开 LNA 最高三档增益 (默认 low-power 模式压住), 提升弱信号 SNR。
  return radio.setHighSensitivityMode(true);
}

int16_t SX1280Radio::exitScanMode()
{
  if (!_begun) return RADIOLIB_ERR_UNKNOWN;
  radio.standby();
  radio.setHighSensitivityMode(false);   // 恢复默认 low-power 增益, 与上电一致
  return radio.switchPacketType(RADIOLIB_SX128X_PACKET_TYPE_LORA);
}

int16_t SX1280Radio::tx(const uint8_t* data, size_t len)
{
  if (!_begun) return RADIOLIB_ERR_UNKNOWN;
  return radio.transmit(data, len);
}

int16_t SX1280Radio::rx(uint8_t* buf, size_t maxLen, uint32_t timeoutMs, size_t* outLen)
{
  if (!_begun) return RADIOLIB_ERR_UNKNOWN;
  // timeout 以微秒为单位; 该版本 RadioLib receive(timeout=0) 的自动超时路径有缺陷,
  // 故要求调用方显式给出毫秒数。
  int16_t s = radio.receive(buf, maxLen, (RadioLibTime_t)timeoutMs * 1000);
  if (s == RADIOLIB_ERR_NONE && outLen) {
    *outLen = radio.getPacketLength(false);
  }
  return s;
}

int16_t SX1280Radio::startRx()
{
  if (!_begun) return RADIOLIB_ERR_UNKNOWN;
  return radio.startReceive();
}

int16_t SX1280Radio::readPacket(uint8_t* buf, size_t maxLen, size_t* outLen)
{
  if (!_begun) return RADIOLIB_ERR_UNKNOWN;
  size_t n = radio.getPacketLength(true);
  size_t read = (n < maxLen) ? n : maxLen;
  int16_t s = radio.readData(buf, read);
  if (s == RADIOLIB_ERR_NONE && outLen) *outLen = n;
  return s;
}

bool SX1280Radio::available()
{
  if (!_begun) return false;
  return (radio.getIrqStatus() & RADIOLIB_SX128X_IRQ_RX_DONE) != 0;
}

int16_t SX1280Radio::armSleepRx()
{
  if (!_begun) return RADIOLIB_ERR_UNKNOWN;
  // startReceive() 走连续 RX (RX_TIMEOUT_INF), DIO1 仅映射 RX_DONE
  // (RADIOLIB_IRQ_RX_DEFAULT_MASK = RX_DONE), 不会因 preamble/sync 噪声误唤醒。
  return radio.startReceive();
}

bool SX1280Radio::readPending(uint8_t* buf, size_t maxLen, size_t* outLen)
{
  if (!_begun) return false;
  // 先看 DIO1 引脚电平判断"是否有包待读", 不要调 getIrqStatus()(会清 IRQ):
  // 若先清 IRQ, readData() 里的 CRC 校验会漏看 CRC_ERROR, 把坏包当正常包返回。
  if (digitalRead(lora::PIN_DIO1) == LOW) return false;

  size_t n = radio.getPacketLength(true);
  size_t read = (n < maxLen) ? n : maxLen;
  // readData() 内部: standby + 读 IRQ(清 DIO1) + CRC 校验 + 读 FIFO
  int16_t s = radio.readData(buf, read);
  if (s == RADIOLIB_ERR_NONE && outLen) *outLen = n;
  return (s == RADIOLIB_ERR_NONE);
}

int16_t SX1280Radio::standby()
{
  if (!_begun) return RADIOLIB_ERR_UNKNOWN;
  return radio.standby();
}

int16_t SX1280Radio::transmitDirect()
{
  if (!_begun) return RADIOLIB_ERR_UNKNOWN;
  // 内部 setRfSwitchState(MODE_TX) 会拉高 TXEN、拉低 RXEN → 外置 PA 导通
  return radio.transmitDirect();
}

float SX1280Radio::rssi()
{
  if (!_begun) return 0.0f;
  return radio.getRSSI();
}

float SX1280Radio::snr()
{
  if (!_begun) return 0.0f;
  return radio.getSNR();
}
