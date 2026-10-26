/**
 * bsp_audio.cpp — I2S 音频板级入口
 */
#include "bsp_audio.h"
#include "bsp_pins.h"

#include <Arduino.h>

#include "I2S/MAX98357.h"
#include "I2S/ZTS6672.h"

namespace {

bool s_ready = false;

/* 256 帧 × 2 声道 × 4 字节 = 2 KB */
int32_t s_raw[512];
int32_t s_tx[512];

/* 0 = 非阻塞；极大值 = 一直等 */
TickType_t to_ticks(uint32_t ms)
{
    return (ms >= 0x7FFFFFFFUL) ? portMAX_DELAY : pdMS_TO_TICKS(ms);
}

}  // namespace

bool bsp_audio_init(void)
{
    if (s_ready) {
        return true;
    }

    const bool spk = MAX98357Manager::getInstance()->init();
    const bool mic = ZTS6672Manager::getInstance()->init();
    s_ready = spk || mic;

    Serial.printf("[BSP] audio: speaker=%d mic=%d\n", spk, mic);
    return s_ready;
}

void bsp_audio_set_enabled(bool en)
{
    MAX98357Manager::getInstance()->setEnabled(en);
}

void bsp_audio_set_volume(uint8_t percent)
{
    MAX98357Manager::getInstance()->setVolume(percent);
}

uint8_t bsp_audio_get_volume(void)
{
    return MAX98357Manager::getInstance()->getVolume();
}

bool bsp_audio_set_sample_rate(uint32_t hz)
{
    return audioI2SSetSampleRate(hz);
}

uint32_t bsp_audio_get_sample_rate(void)
{
    return audioI2SGetSampleRate();
}

void bsp_audio_beep(uint16_t freq_hz, uint16_t dur_ms)
{
    if (s_ready) {
        MAX98357Manager::getInstance()->beep(freq_hz, dur_ms);
    }
}

size_t bsp_audio_write_pcm16(const int16_t *pcm, size_t frames,
                             uint32_t timeout_ms)
{
    if (!s_ready || !pcm || frames == 0) {
        return 0;
    }

    MAX98357Manager *spk = MAX98357Manager::getInstance();
    if (!spk->isEnabled()) {
        return 0;
    }

    const TickType_t timeout = to_ticks(timeout_ms);
    if (!spk->lockTimeout(timeout)) {
        return 0;
    }

    const float gain = spk->volumeGain();
    const size_t chunk = sizeof(s_tx) / (2 * sizeof(s_tx[0]));
    size_t done = 0;

    while (done < frames) {
        const size_t n = (frames - done < chunk) ? (frames - done) : chunk;
        for (size_t i = 0; i < n; ++i) {
            const int16_t scaled = (int16_t)((float)pcm[done + i] * gain);
            const int32_t s = toI2S32(scaled);
            s_tx[i * 2]     = s;   // 左声道 (MAX98357 取左)
            s_tx[i * 2 + 1] = s;   // 右声道
        }

        const size_t want = n * 2 * sizeof(int32_t);
        const size_t got = spk->write(s_tx, want, timeout);
        done += got / (2 * sizeof(int32_t));
        if (got < want) {
            break;
        }
    }

    spk->unlock();
    return done;
}

size_t bsp_audio_read_pcm16(int16_t *out, size_t max_frames,
                            uint32_t timeout_ms)
{
    if (!s_ready || !out || max_frames == 0) {
        return 0;
    }

    const size_t capacity = sizeof(s_raw) / (2 * sizeof(s_raw[0]));
    const size_t frames = (max_frames < capacity) ? max_frames : capacity;

    const size_t bytes = ZTS6672Manager::getInstance()->read(
        s_raw, frames * 2 * sizeof(int32_t), to_ticks(timeout_ms));
    const size_t got = bytes / (2 * sizeof(int32_t));

    for (size_t i = 0; i < got; ++i) {
        out[i] = (int16_t)(s_raw[i * 2] >> 16);
    }
    return got;
}
