/**
 * ZTS6672 I2S 数字麦克风 — 采集 (录音)
 *
 * 接线 (与 MAX98357 功放共用同一 I2S 总线，引脚见 bsp_pins.h):
 *   BCLK  ← I2S 位时钟
 *   LRC   ← I2S 帧时钟 / 声道选择
 *   DOUT  → ESP32 DIN (麦克风数据输出)
 *
 * 与播放共用 BCLK/LRC, I2S 全双工安装 (见 MAX98357.h 的 audioI2SInit)。
 * 兼容 INMP441: 24-bit 输出, I2S 以 32-bit/样本、立体声(RIGHT_LEFT)接收, 数据左对齐;
 * 麦克风数据在左声道 (读回缓冲的偶数下标), 用 fromI2S32() 取高 16 位转 PCM16。
 */
#ifndef I2S_ZTS6672_H
#define I2S_ZTS6672_H

#include <Arduino.h>
#include <stddef.h>

class ZTS6672Manager {
public:
    static ZTS6672Manager* getInstance();

    bool   init();                       // 确保共享 I2S 就绪
    bool   isReady() const;

    // 返回 32-bit 原始样本 (24-bit 数据左对齐); 用 MAX98357.h 的 fromI2S32() 转 16-bit
    size_t read(void *buf, size_t bytes, TickType_t timeout = portMAX_DELAY); // i2s_read
    void   flush();                      // 清空 RX DMA

private:
    ZTS6672Manager() {}
    bool _ready = false;
};

#endif
