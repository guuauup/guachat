/**
 * bsp_radio.cpp — SX1280Radio 的板级入口
 */
#include "bsp_radio.h"

#include <RadioLib.h>

#include "SX1280/RadioConfig.h"
#include "SX1280/SX1280Radio.h"

namespace {

SX1280Radio s_radio;
bool s_ready = false;

float   s_freq_mhz = lora::DEFAULT_FREQ_MHZ;
uint8_t s_sf       = lora::DEFAULT_SF;
float   s_bw_khz   = lora::DEFAULT_BW_KHZ;

}  // namespace

bool bsp_radio_init(void)
{
    if (s_ready) {
        return true;
    }
    s_ready = s_radio.begin();
    if (!s_ready) {
        Serial.println("[BSP] radio: SX1280 init failed");
    } else {
        bsp_radio_start_rx();
    }
    return s_ready;
}

bool bsp_radio_start_rx(void)
{
    return s_ready && s_radio.startRx() == RADIOLIB_ERR_NONE;
}

bool bsp_radio_set_params(float freq_mhz, uint8_t sf, float bw_khz,
                          int8_t power_dbm)
{
    if (!s_ready) {
        return false;
    }

    const float freq = lora::clampFreqFloat(freq_mhz);
    if (!s_radio.applyParams(freq, sf, bw_khz, power_dbm)) {
        return false;
    }

    s_freq_mhz = freq;
    s_sf       = sf;
    s_bw_khz   = bw_khz;
    return true;
}

bool bsp_radio_send(const uint8_t *data, size_t len)
{
    if (!s_ready || !data || len == 0) {
        return false;
    }
    return s_radio.tx(data, len) == RADIOLIB_ERR_NONE;
}

bool bsp_radio_available(void)
{
    return s_ready && s_radio.available();
}

int bsp_radio_read(uint8_t *buf, size_t max_len, size_t *out_len)
{
    if (!s_ready) {
        return RADIOLIB_ERR_UNKNOWN;
    }
    return s_radio.readPacket(buf, max_len, out_len);
}

int bsp_radio_receive(uint8_t *buf, size_t max_len, uint32_t timeout_ms,
                      size_t *out_len)
{
    if (!s_ready) {
        return RADIOLIB_ERR_UNKNOWN;
    }
    return s_radio.rx(buf, max_len, timeout_ms, out_len);
}

void bsp_radio_info(bsp_radio_info_t *out)
{
    if (!out) {
        return;
    }
    *out = {};
    out->ready     = s_ready;
    out->freq_mhz  = s_freq_mhz;
    out->sf        = s_sf;
    out->bw_khz    = s_bw_khz;
    out->power_dbm = s_radio.powerDbm();
}
