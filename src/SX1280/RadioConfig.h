/**
 * RadioConfig.h — SX1280 (E28-2G4M20S) 射频常量
 *
 * 模块：E28-2G4M20S（SX1280 + 外置 PA/LNA，固定增益约 22 dB，板载天线）。
 * 默认/推荐发射功率 7 dBm，频率边界 2402~2482 MHz，为 2.4 GHz ISM 频段留出边缘余量。
 * 固件不把 7 dBm 作为策略上限：应用可以请求更高功率，最高夹到模块硬件上限
 * 20 dBm；高于推荐值时只在串口提示风险，不阻止用户。与 WiFi/BLE 并发时，
 * 7 dBm 是唯一验证过的条件，更高功率近距离可能损坏 ESP32-S3 的 LNA；同时应
 * 让 LoRa 频率远离 WiFi 信道。默认 2405 MHz 靠近 WiFi ch1（2412 MHz），WiFi
 * 用 ch1 时建议改到远离该信道的频率。
 *
 * 引脚映射以 bsp/bsp_pins.h 为唯一来源；本文件只放射频参数，
 * 不包含 RadioLib / SPI 代码。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "bsp/bsp_pins.h"

namespace lora {

// ============================================================
// 引脚映射（来自 bsp_pins.h）
// ============================================================
constexpr int PIN_MISO   = BSP_LORA_PIN_MISO;
constexpr int PIN_MOSI   = BSP_LORA_PIN_MOSI;
constexpr int PIN_SCK    = BSP_LORA_PIN_SCK;
constexpr int PIN_NSS    = BSP_LORA_PIN_NSS;
constexpr int PIN_DIO1   = BSP_LORA_PIN_DIO1;
constexpr int PIN_RXEN   = BSP_LORA_PIN_RXEN;
constexpr int PIN_TXEN   = BSP_LORA_PIN_TXEN;
constexpr int PIN_NRESET = BSP_LORA_PIN_NRESET;
constexpr int PIN_BUSY   = BSP_LORA_PIN_BUSY;

// ============================================================
// LoRa 调制参数
// ============================================================
constexpr uint8_t LORA_CR        = 5;      // 编码率 4/5（RadioLib 使用分母）
constexpr uint8_t LORA_PREAMBLE  = 8;      // 前导码 8 符号
constexpr uint8_t LORA_SYNC_WORD = 0x12;   // 私有同步字，可改以区分邻近网络

// 单频工作时的默认参数
constexpr float   DEFAULT_FREQ_MHZ  = 2405.0f;
constexpr uint8_t DEFAULT_SF        = 10;
constexpr float   DEFAULT_BW_KHZ    = 812.5f;
constexpr int8_t  DEFAULT_POWER_DBM = BSP_LORA_POWER_RECOMMENDED_DBM;

// ============================================================
// 合规边界（由 bsp_pins.h 提供）
// ============================================================
constexpr int8_t   TX_POWER_MAX_DBM         = BSP_LORA_POWER_MAX_DBM;
constexpr int8_t   TX_POWER_RECOMMENDED_DBM = BSP_LORA_POWER_RECOMMENDED_DBM;
constexpr uint16_t FREQ_MIN_MHZ             = BSP_LORA_FREQ_MIN_MHZ;
constexpr uint16_t FREQ_MAX_MHZ             = BSP_LORA_FREQ_MAX_MHZ;
constexpr float    FREQ_MIN_FLOAT           = (float)FREQ_MIN_MHZ;
constexpr float    FREQ_MAX_FLOAT           = (float)FREQ_MAX_MHZ;

constexpr float clampFreqFloat(float f)
{
  return (f < FREQ_MIN_FLOAT) ? FREQ_MIN_FLOAT
                              : ((f > FREQ_MAX_FLOAT) ? FREQ_MAX_FLOAT : f);
}

constexpr int8_t clampTxPower(int8_t dbm)
{
  return (dbm > TX_POWER_MAX_DBM) ? TX_POWER_MAX_DBM : dbm;
}

static_assert(DEFAULT_FREQ_MHZ >= FREQ_MIN_FLOAT &&
              DEFAULT_FREQ_MHZ <= FREQ_MAX_FLOAT,
              "默认频率必须在 2402~2482 MHz 合规边界内");
static_assert(DEFAULT_POWER_DBM <= TX_POWER_MAX_DBM,
              "默认功率不得超过模块级上限");
static_assert(TX_POWER_RECOMMENDED_DBM <= TX_POWER_MAX_DBM,
              "推荐功率不得超过模块硬件上限");
static_assert(TX_POWER_MAX_DBM <= 20,
              "LoRa 模块级功率不得超过 E28-2G4M20S 的 20 dBm 硬件上限");

// ============================================================
// 模块 PA 功率映射（须用功率计实测校准）
//
// RadioLib 的 SX1280 setOutputPower() 范围是 -18..+13 dBm（芯片级）。
// E28-2G4M20S 的外置 PA 为固定增益、顶端饱和，降功率靠压低芯片输出：
//   E28-2G4M20S：PA 增益约 +22 dB → 芯片 -2 dBm 时天线口约 20 dBm
//   映射：chipDbm = moduleDbm - PA_GAIN_M20S
// 低端增益可能非线性，量产前应在天线口逐档实测。
// ============================================================
constexpr int8_t PA_GAIN_M20S  = 22;
constexpr int8_t CHIP_MIN_DBM  = -18;
constexpr int8_t CHIP_MAX_DBM  = 13;

} // namespace lora
