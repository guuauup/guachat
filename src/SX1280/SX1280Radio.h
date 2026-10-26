/**
 * SX1280Radio.h — SX1280 (E28-2G4M20S) RadioLib 封装
 *
 * 职责：独立 SPI 总线、模块初始化、频率/扩频/带宽/功率下发、CAD、收发、RSSI/SNR。
 * 不做：协议、分片、重传、去重或 CSMA 状态机。
 *
 * 默认参数：2405 MHz / SF10 / BW812.5 / CR4/5 / +7 dBm。
 * 频率与功率硬件边界由 RadioConfig.h 钳位；高于推荐功率只提示风险。
 *
 * 用法：
 *   SX1280Radio radio;
 *   radio.begin();
 *   radio.setPower(7);          // 模块级 dBm；>7 提示风险，>20 夹到硬件上限
 *   radio.tx(buf, len);
 *   size_t n; radio.rx(rxbuf, sizeof(rxbuf), 2000, &n);
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "RadioConfig.h"

// 单点频谱采样统计结果 (scanRssiAt 输出)
struct ScanSample {
  int8_t  meanDbm;      // 平均 RSSI (dBm)
  int8_t  peakDbm;      // 峰值 RSSI (dBm)，突发检测
  int8_t  minDbm;       // 底噪 RSSI (dBm)
  uint8_t occupancyPct; // 占空比 0..100 (%)：采样中高于阈值的比例
};

class SX1280Radio {
public:
  SX1280Radio();

  /// 初始化 SPI、模块和默认调制参数；返回 true 表示芯片正常
  bool begin();

  /// 设置发射功率（模块级 dBm）；高于推荐值只提示，最高夹到硬件上限
  bool setPower(int8_t dBm);

  /// 一次下发频率/扩频/带宽/功率
  bool applyParams(float freqMHz, uint8_t sf, float bwKhz, int8_t powerDbm);

  /// 当前功率（模块级 dBm），供上层回读
  int8_t powerDbm() const { return _powerDbm; }

  /// 阻塞式信道活动检测 (CAD)，用于 CSMA
  /// 返回 RADIOLIB_CHANNEL_FREE / RADIOLIB_CHANNEL_DETECTED / 错误码
  int16_t startCAD();

  /// 单点频谱采样：切到 freqMHz → 连续接收 → 采样 samples 次
  /// (间隔 sampleUs µs) 统计均值/峰值/底噪/占空比。须先 enterScanMode()。
  int16_t scanRssiAt(float freqMHz, uint16_t samples, uint32_t sampleUs,
                     int8_t busyThresholdDbm, ScanSample* out);

  /// 进入频谱扫描模式：切 GFSK + 2.4 MHz 宽信道滤波，使 getRSSI(false)
  /// 成为平坦能量检测。LoRa 的 GetRssiInst 是 chirp 基带值，对宽带信号不准确。
  int16_t enterScanMode();

  /// 退出扫描模式：切回 LoRa；调制参数由调用方随后 applyParams() 恢复。
  int16_t exitScanMode();

  /// 阻塞式发送，返回 RADIOLIB_ERR_NONE 表示完成
  int16_t tx(const uint8_t* data, size_t len);

  /// 阻塞式接收
  /// timeoutMs 须显式给出 (RadioLib 该版本 receive(timeout=0) 自动超时路径有缺陷)
  /// 成功时 *outLen = 实际收到的字节数
  int16_t rx(uint8_t* buf, size_t maxLen, uint32_t timeoutMs, size_t* outLen);

  /// 进入连续接收
  int16_t startRx();

  /// 收到包后读取 (配合 startRx + DIO1 中断)
  int16_t readPacket(uint8_t* buf, size_t maxLen, size_t* outLen);

  /// 是否有包待读 (轮询 DIO1 的 RX_DONE 标志)
  bool available();

  /// 进入连续接收 (睡眠停靠前调用；DIO1 仅映射 RX_DONE)
  int16_t armSleepRx();

  /// 唤醒后读取睡眠期间已收到的包 (不先重挂 RX，避免清 FIFO 丢包)。
  /// 返回 true 表示读到完整包 (CRC 通过)。
  bool readPending(uint8_t* buf, size_t maxLen, size_t* outLen);

  /// 进入待机
  int16_t standby();

  /// 连续波发射 (CW 载波)，便于电流与功率测试
  /// 须用 standby() 退出；CW 会一直发射，勿长时间保持以免发热
  int16_t transmitDirect();

  /// 最后一次收包的 RSSI (dBm) / SNR (dB)
  float rssi();
  float snr();

private:
  int8_t toChipDbm(int8_t moduleDbm) const;  // 模块级 → 芯片级 dBm

  bool   _begun;
  int8_t _powerDbm;
};
