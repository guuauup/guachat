/**
 * MAX98357 I2S Class-D 功放 — 播放 (扬声器)
 * 共享 I2S 总线 (BCLK/LRC) 与 ZTS6672 麦克风复用, 全双工安装。
 */
#include "MAX98357.h"
#include <driver/i2s.h>
#include <driver/gpio.h>
#include <freertos/semphr.h>
#include <math.h>

#define AUDIO_I2S_NUM   I2S_NUM_0

static MAX98357Manager *_inst = nullptr;
static bool _i2sInstalled = false;
static uint32_t _sampleRate = BSP_I2S_SAMPLE_RATE;

// ── 共享 I2S 总线初始化 (全双工 TX+RX, 幂等)。MAX98357/ZTS6672 共用。 ──
bool audioI2SInit(void) {
    if (_i2sInstalled) return true;

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
    cfg.sample_rate          = _sampleRate;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT;    // 32-bit (麦克风 24-bit 左对齐)
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;   // 立体声帧 (BCLK=fs×64)
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;    // 标准 I2S (Philips) — MAX98357A 正确
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = 256;    // 256 帧 × 2ch × 4B = 2KB/buf; 环 2048 帧深缓冲, 抗任务长时抢占避免 DMA 欠载
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;   // 欠载自动清描述符, 避免噪声
    cfg.fixed_mclk           = 0;

    esp_err_t err = i2s_driver_install(AUDIO_I2S_NUM, &cfg, 0, nullptr);
    if (err != ESP_OK) {
        Serial.printf("[I2S] driver_install 失败: %s\n", esp_err_to_name(err));
        return false;
    }

    i2s_pin_config_t pin = {};
    pin.bck_io_num   = AUDIO_I2S_BCLK_PIN;
    pin.ws_io_num    = AUDIO_I2S_LRC_PIN;
    pin.data_out_num = AUDIO_I2S_DOUT_PIN;   // → MAX98357 DIN
    pin.data_in_num  = AUDIO_I2S_DIN_PIN;    // ← ZTS6672 DOUT
    pin.mck_io_num   = I2S_PIN_NO_CHANGE;

    err = i2s_set_pin(AUDIO_I2S_NUM, &pin);
    if (err != ESP_OK) {
        Serial.printf("[I2S] set_pin 失败: %s\n", esp_err_to_name(err));
        // 回滚驱动安装: 否则 _i2sInstalled 仍为 false, 下次重试 driver_install
        // 会返回 ESP_ERR_INVALID_STATE, 永远装不上。
        i2s_driver_uninstall(AUDIO_I2S_NUM);
        return false;
    }

    _i2sInstalled = true;
    Serial.printf("[I2S] 全双工 TX+RX 就绪 (%uHz, 32bit立体声, BCLK=%uHz)\n",
                  (unsigned)_sampleRate, (unsigned)(_sampleRate * 64));
    return true;
}

bool audioI2SSetSampleRate(uint32_t hz) {
    if (hz < 8000 || hz > 96000) return false;

    if (!_i2sInstalled) {
        _sampleRate = hz;   // 尚未安装: 记录，audioI2SInit() 时使用
        return true;
    }

    esp_err_t err = i2s_set_clk(AUDIO_I2S_NUM, hz,
                                I2S_BITS_PER_SAMPLE_32BIT,
                                I2S_CHANNEL_STEREO);
    if (err != ESP_OK) {
        Serial.printf("[I2S] set_clk(%uHz) 失败: %s\n", (unsigned)hz,
                      esp_err_to_name(err));
        return false;
    }

    _sampleRate = hz;
    return true;
}

uint32_t audioI2SGetSampleRate(void) {
    return _sampleRate;
}

MAX98357Manager* MAX98357Manager::getInstance() {
    if (!_inst) _inst = new MAX98357Manager();
    return _inst;
}

bool MAX98357Manager::init() {
    if (!audioI2SInit()) return false;
    if (!_audioMutex) _audioMutex = xSemaphoreCreateMutex();

    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << MAX98357_SD_PIN);
    io.mode         = GPIO_MODE_OUTPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_ENABLE;   // 上电默认拉低=静音, 初始化后置高=左声道
    gpio_config(&io);
    setEnabled(true);

    Serial.println("[MAX98357] 功放已使能 (SD_MODE=1, 左声道)");
    return true;
}

void MAX98357Manager::setEnabled(bool en) {
    _enabled = en;
    gpio_set_level((gpio_num_t)MAX98357_SD_PIN, en ? 1 : 0);
    if (!en) flush();   // 关断前清空 DMA
}

bool MAX98357Manager::isEnabled() const { return _enabled; }

void MAX98357Manager::setVolume(uint8_t percent) {
    _volume = (percent > 100) ? 100 : percent;
}

size_t MAX98357Manager::write(const void *data, size_t bytes, TickType_t timeout) {
    if (!_enabled) return 0;
    size_t written = 0;
    esp_err_t err = i2s_write(AUDIO_I2S_NUM, data, bytes, &written, timeout);
    return (err == ESP_OK) ? written : 0;
}

void MAX98357Manager::flush() {
    lock();
    flushRaw();
    unlock();
}

// 独占锁 (供长音频独占播放): 期间其他 write/beep 会阻塞
void MAX98357Manager::lock() {
    if (_audioMutex) xSemaphoreTake(_audioMutex, portMAX_DELAY);
}

void MAX98357Manager::unlock() {
    if (_audioMutex) xSemaphoreGive(_audioMutex);
}

// 非阻塞取锁: 失败 = 有其它播放正在独占, 调用方可跳过本次播放
bool MAX98357Manager::tryLock() {
    if (!_audioMutex) return true;
    return xSemaphoreTake(_audioMutex, 0) == pdTRUE;
}

// 限时取锁: 等当前播放让出锁; 超时则放弃，不阻塞调用方。
bool MAX98357Manager::lockTimeout(TickType_t ticks) {
    if (!_audioMutex) return true;
    return xSemaphoreTake(_audioMutex, ticks) == pdTRUE;
}

// 清空 TX DMA (不加锁, 需在 lock() 后调用)
void MAX98357Manager::flushRaw() {
    if (_i2sInstalled) i2s_zero_dma_buffer(AUDIO_I2S_NUM);
}

void MAX98357Manager::beep(uint16_t freqHz, uint16_t durMs) {
    if (!_enabled || freqHz == 0 || durMs == 0) return;
    if (!lockTimeout(pdMS_TO_TICKS(50))) return;   // 等当前播放让锁; 长音频独占则放弃
    flushRaw();               // 清残留, 立即播本蜂鸣

    const uint32_t total = (uint32_t)_sampleRate * durMs / 1000;
    const float phaseInc = 2.0f * (float)PI * freqHz / _sampleRate;
    const float amplitude = 32000.0f * volumeGain();   // 16-bit 幅度 × 软件音量 (峰值 32000, 近满幅 32767)

    // 淡入/淡出包络 (各 ~5ms): 避免全幅度正弦突发在功放输出滤波上产生开关爆音 ("断电"声)
    uint32_t fade = (uint32_t)_sampleRate * 5 / 1000;   // 40 样本 @8kHz
    if (fade > total / 2) fade = total / 2;                    // 极短鸣音时防包络重叠

    const uint32_t CHUNK = 32;                  // 32 立体声样本/块 = 256B 栈 (任务栈安全)
    int32_t buf[CHUNK * 2];                     // 32-bit 左右声道交替 (L=R)
    float phase = 0.0f;

    for (uint32_t n = 0; n < total; n += CHUNK) {
        uint32_t cnt = (total - n) < CHUNK ? (total - n) : CHUNK;
        for (uint32_t i = 0; i < cnt; i++) {
            uint32_t idx = n + i;
            float env = 1.0f;
            if (idx < fade) {
                env = (float)idx / (float)fade;                          // 淡入 0→1
            } else if (idx >= total - fade) {
                env = (float)(total - 1 - idx) / (float)(fade - 1);      // 淡出 1→0 (末样本=0)
            }
            // 基波 + 少量 2 次谐波: 纯正弦低音量下显"闷", 谐波让音色更亮/更"透"
            float v = (sinf(phase) + 0.25f * sinf(2.0f * phase)) * (1.0f / 1.10f);
            int32_t s = toI2S32((int16_t)(v * amplitude * env));
            buf[i * 2]     = s;                 // 左声道 (MAX98357 取左)
            buf[i * 2 + 1] = s;                 // 右声道 (相同)
            phase += phaseInc;
        }
        write(buf, cnt * 2 * sizeof(int32_t));
    }
    // 尾部补零: 延长静音让 DMA 干净下潜, 避免截断爆音
    memset(buf, 0, sizeof(buf));
    for (int k = 0; k < 4; k++) write(buf, sizeof(buf));   // 128 帧静音
    unlock();
}

// 播放预渲染的整段波形 (int32 左右交错, 满幅) — 互斥 + 应用软件音量
// 适合提前渲染好的音阶/提示音: 一次 i2s_write 后由 DMA 自主播完
void MAX98357Manager::playBuffer(const int32_t *buf, size_t frames) {
    if (!_enabled || !buf || frames == 0) return;
    if (_audioMutex) xSemaphoreTake(_audioMutex, portMAX_DELAY);

    const float gain = volumeGain();
    if (gain >= 1.0f - 1e-6f) {
        write(buf, frames * 2 * sizeof(int32_t));
    } else {
        // 音量 < 100%: 缩放左对齐 32-bit 样本 (低 16 位恒 0), 用静态暂存避免污染预渲染缓冲
        static int32_t scratch[640 * 2];   // 缩放暂存 (防御上限)
        size_t n = frames * 2;
        if (n > sizeof(scratch) / sizeof(scratch[0])) {
            write(buf, frames * 2 * sizeof(int32_t));   // 超长则忽略缩放, 直接播 (防御)
        } else {
            for (size_t i = 0; i < n; i++)
                scratch[i] = (int32_t)((int16_t)((buf[i] >> 16) * gain)) << 16;
            write(scratch, frames * 2 * sizeof(int32_t));
        }
    }

    if (_audioMutex) xSemaphoreGive(_audioMutex);
}

// 写静音保持 I2S DMA 不断流: 在每声 beep 之间的空隙持续喂静音,
// 让 DMA 永不欠载, 消除欠载→重启的开关瞬态 ("断电噗声")。
void MAX98357Manager::writeSilence(size_t frames) {
    if (!_enabled || frames == 0) return;
    if (_audioMutex) xSemaphoreTake(_audioMutex, portMAX_DELAY);
    static const int32_t zeros[32 * 2] = {0};   // 32 帧零样本
    size_t done = 0;
    while (done < frames) {
        size_t n = (frames - done) < 32 ? (frames - done) : 32;
        write(zeros, n * 2 * sizeof(int32_t));
        done += n;
    }
    if (_audioMutex) xSemaphoreGive(_audioMutex);
}

// 连续音: 从无缝循环块持续喂 tone 数据, 保持 I2S DMA 不断流。
// 循环块应为整周期波形, 跨块才能无缝; pos 在块内循环推进。
void MAX98357Manager::writeTone(const int32_t *buf, size_t loopFrames, size_t &pos, size_t frames) {
    if (_audioMutex) xSemaphoreTake(_audioMutex, portMAX_DELAY);
    writeToneRaw(buf, loopFrames, pos, frames);
    if (_audioMutex) xSemaphoreGive(_audioMutex);
}

// 流式喂循环块 (不加锁): 供 lock() 后的独占播放复用
void MAX98357Manager::writeToneRaw(const int32_t *buf, size_t loopFrames, size_t &pos, size_t frames) {
    if (!_enabled || !buf || loopFrames == 0 || frames == 0) return;
    const float gain = volumeGain();
    static int32_t scratch[32 * 2];   // 缩放暂存 (32 帧)
    size_t done = 0;
    while (done < frames) {
        size_t remain = loopFrames - pos;
        size_t n = (frames - done) < remain ? (frames - done) : remain;
        if (n > 32) n = 32;
        if (gain >= 1.0f - 1e-6f) {
            write(buf + pos * 2, n * 2 * sizeof(int32_t));
        } else {
            for (size_t i = 0; i < n * 2; i++)
                scratch[i] = (int32_t)((int16_t)((buf[pos * 2 + i] >> 16) * gain)) << 16;
            write(scratch, n * 2 * sizeof(int32_t));
        }
        pos = (pos + n) % loopFrames;
        done += n;
    }
}
