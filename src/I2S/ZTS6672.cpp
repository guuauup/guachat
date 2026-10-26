/**
 * ZTS6672 I2S 数字麦克风 — 采集 (录音)
 * 与 MAX98357 播放共用 I2S 总线 (BCLK/LRC), 走全双工 RX 通道。
 */
#include "ZTS6672.h"
#include "MAX98357.h"   // audioI2SInit + 共享引脚/参数
#include <driver/i2s.h>

#define AUDIO_I2S_NUM   I2S_NUM_0

static ZTS6672Manager *_inst = nullptr;

ZTS6672Manager* ZTS6672Manager::getInstance() {
    if (!_inst) _inst = new ZTS6672Manager();
    return _inst;
}

bool ZTS6672Manager::init() {
    _ready = audioI2SInit();
    if (_ready) Serial.println("[ZTS6672] 麦克风就绪 (I2S RX)");
    return _ready;
}

bool ZTS6672Manager::isReady() const { return _ready; }

size_t ZTS6672Manager::read(void *buf, size_t bytes, TickType_t timeout) {
    if (!_ready) return 0;
    size_t got = 0;
    esp_err_t err = i2s_read(AUDIO_I2S_NUM, buf, bytes, &got, timeout);
    return (err == ESP_OK) ? got : 0;
}

void ZTS6672Manager::flush() {
    if (_ready) i2s_zero_dma_buffer(AUDIO_I2S_NUM);
}
