/**
 * MAX98357 I2S Class-D 功放 — 播放 (扬声器)
 *
 * 接线 (与 ZTS6672 麦克风共用同一 I2S 总线)，引脚见 bsp_pins.h:
 *   BCLK    ← I2S 位时钟
 *   LRC     ← I2S 帧时钟 / 声道选择
 *   DIN     ← ESP32 DOUT (串行数据输出)
 *   SD_MODE → 高=左声道(工作), 低=关闭/静音
 *
 * ZTS6672 的 DOUT 接 ESP32 DIN，与播放共用 BCLK/LRC，
 * 因此 I2S 以「全双工 (TX+RX)」安装一次。
 *
 * 传输格式固定为 32-bit/样本、立体声(RIGHT_LEFT):
 *   - 播放: 16-bit PCM 左移到高 16 位 (toI2S32), 左右声道写相同数据 (功放取左声道)。
 *   - 采集: 麦克风输出在左声道, 24-bit 取高 16 位 (fromI2S32)。
 * 立体声帧让 BCLK = fs×64。默认 fs=8 kHz (BSP_I2S_SAMPLE_RATE)，可按应用通过
 * audioI2SSetSampleRate() 调整。
 */
#ifndef I2S_MAX98357_H
#define I2S_MAX98357_H

#include <Arduino.h>
#include <stddef.h>

#include "bsp/bsp_pins.h"

// ── I2S 总线引脚 (MAX98357 与 ZTS6672 共用) ──
#define AUDIO_I2S_BCLK_PIN   BSP_I2S_PIN_BCLK
#define AUDIO_I2S_LRC_PIN    BSP_I2S_PIN_LRC
#define AUDIO_I2S_DOUT_PIN   BSP_I2S_PIN_DOUT   // ESP32 → MAX98357 DIN
#define AUDIO_I2S_DIN_PIN    BSP_I2S_PIN_DIN    // ESP32 ← ZTS6672 DOUT

// I2S 统一 32-bit/样本 (见 audioI2SInit): 麦克风 24-bit 左对齐在 32-bit 帧里
static inline int32_t toI2S32(int16_t pcm)   { return ((int32_t)pcm) << 16; }  // 16-bit PCM → 32-bit 左对齐 (播放)
static inline int16_t fromI2S32(int32_t raw) { return (int16_t)(raw >> 16); }  // 32-bit → 16-bit (采集: 24-bit 取高 16)

// ── MAX98357 专用 ──
#define MAX98357_SD_PIN      BSP_I2S_PIN_SD_MODE   // SD_MODE: 高=左声道(工作), 低=关闭/静音

/** 共享 I2S 总线初始化 (全双工 TX+RX, 幂等)。MAX98357/ZTS6672 共用。 */
bool audioI2SInit(void);

/** 修改采样率 (Hz)。未初始化时只记录，初始化时生效；已初始化时立即重配时钟。
 *  建议在没有播放/录音时调用，范围 8000..96000。 */
bool audioI2SSetSampleRate(uint32_t hz);

/** 当前采样率 (Hz) */
uint32_t audioI2SGetSampleRate(void);

// ── 各音量档位峰值幅度 (0=静音, 10=满幅 32000) ──
// 等 dB 步进 (~+2.3dB/档): 低档位也能清晰听见, 高档位不再"无感"
static const int32_t kVolumeGainByLevel[11] = {
    0, 2800, 3900, 5100, 6600, 8600, 11200, 14600, 18900, 24600, 32000
};

class MAX98357Manager {
public:
    static MAX98357Manager* getInstance();

    bool   init();                       // 安装共享 I2S + 使能功放
    void   setEnabled(bool en);          // SD_MODE (高=左声道工作, 低=静音)
    bool   isEnabled() const;

    // 音量 0..100%: MAX98357 无硬件音量, 纯软件增益 (PCM 幅度缩放, 0%=静音)
    void    setVolume(uint8_t percent);
    uint8_t getVolume() const { return _volume; }
    float   volumeGain() const {
        uint8_t lvl = (uint8_t)(_volume / 10);          // 档位 0..10
        if (lvl > 10) lvl = 10;
        return (float)kVolumeGainByLevel[lvl] / 32000.0f;  // 查表: 等 dB 步进, 低档位也可用
    }

    size_t write(const void *data, size_t bytes, TickType_t timeout = portMAX_DELAY);
    void   flush();                      // 清空 TX DMA (停止/静音)
    void   beep(uint16_t freqHz, uint16_t durMs);  // 播一声短鸣 (幅度跟随当前音量)
    void   playBuffer(const int32_t *buf, size_t frames);  // 播放预渲染整段波形 (单次 i2s_write, 互斥)
    void   writeSilence(size_t frames);   // 写静音保持 DMA 不断流, 避免欠载瞬态"断电噗声"
    void   writeTone(const int32_t *buf, size_t loopFrames, size_t &pos, size_t frames);  // 连续音: 从无缝循环块持续喂 tone
    void   writeToneRaw(const int32_t *buf, size_t loopFrames, size_t &pos, size_t frames); // 流式喂循环块 (不加锁, 供独占播放用)

    // ── 独占播放 (长音频/连续音): lock → flushRaw → write → unlock ──
    void   lock();                       // 取互斥锁 (期间其他播放阻塞)
    void   unlock();                     // 释放互斥锁
    bool   tryLock();                    // 非阻塞取锁 (失败=有其它声音在播)
    bool   lockTimeout(TickType_t ticks);// 限时取锁: 等当前播放让出锁, 超时则放弃不阻塞调用方
    void   flushRaw();                   // 清空 TX DMA (不加锁, 需在 lock 后调用)

private:
    MAX98357Manager() {}
    bool _enabled = false;
    uint8_t _volume = 100;               // 默认满音量 (与当前蜂鸣幅度一致)
    SemaphoreHandle_t _audioMutex = nullptr;   // 串行化播放, 避免并发重叠与播放中被静音截断
};

#endif
