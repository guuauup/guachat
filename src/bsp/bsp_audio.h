/**
 * bsp_audio.h — MAX98357 扬声器 + ZTS6672 麦克风（共享 I2S0）
 *
 * 默认 8 kHz（BSP_I2S_SAMPLE_RATE）/ 32-bit / 立体声，可用
 * bsp_audio_set_sample_rate() 在运行时调整。麦克风为 24-bit 左对齐，
 * 取高 16 bit 得到 PCM16。
 *
 * 播放与录音共用一条 I2S 总线。beep() 会阻塞 dur_ms，不要从 LVGL
 * 定时器里调用长音；长 PCM 请放到独立任务中。
 */
#ifndef BSP_AUDIO_H
#define BSP_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 安装 I2S（全双工）并使能功放 */
bool bsp_audio_init(void);

/** 功放使能（false=静音并清空 TX DMA） */
void bsp_audio_set_enabled(bool en);

/** 软件音量 0..100（MAX98357 无硬件音量） */
void bsp_audio_set_volume(uint8_t percent);

/** 当前软件音量 0..100 */
uint8_t bsp_audio_get_volume(void);

/**
 * 设置采样率（Hz），范围 8000..96000。
 * 未初始化时只记录，初始化时生效；已初始化时立即重配 I2S 时钟。
 * 建议在没有播放/录音时调用。
 */
bool bsp_audio_set_sample_rate(uint32_t hz);

/** 当前采样率（Hz） */
uint32_t bsp_audio_get_sample_rate(void);

/** 播放一声短鸣（阻塞 dur_ms） */
void bsp_audio_beep(uint16_t freq_hz, uint16_t dur_ms);

/**
 * 播放 PCM16 单声道数据（按当前采样率和软件音量）。
 * @param pcm        输入样本
 * @param frames     样本数（每帧 16 bit）
 * @param timeout_ms DMA 等待超时；0=非阻塞
 * @return 实际写入帧数
 */
size_t bsp_audio_write_pcm16(const int16_t *pcm, size_t frames,
                             uint32_t timeout_ms);

/**
 * 从麦克风读 PCM16 单声道。
 * @param out        输出缓冲
 * @param max_frames 最多帧数
 * @param timeout_ms 0=非阻塞
 * @return 实际帧数（每帧 16 bit）
 */
size_t bsp_audio_read_pcm16(int16_t *out, size_t max_frames,
                            uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* BSP_AUDIO_H */
