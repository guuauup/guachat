/**
 * bsp_haptic.cpp — DRV2605L 薄封装
 *
 * 只负责初始化、播放、设置持久化与空闲管理，不包含 UI 或应用逻辑。
 * 设置保存在 NVS 命名空间 "drv2605"，键名与原 GUACHAT-2G4 兼容：
 *   alert_lvl (u8 0..10), alert_eff (u8), kb_on (bool), ui_saved (bool)
 */
#include "bsp_haptic.h"
#include "bsp_pins.h"

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "I2C0/DRV2605L.h"
#include "I2C0/I2CBus.h"

namespace {

constexpr const char *NVS_NAMESPACE = "drv2605";

DRV2605LDriver s_drv;
bool s_ready = false;
bool s_boot_done = false;
bool s_settings_loaded = false;

volatile uint8_t s_alert_level = BSP_HAPTIC_ALERT_LEVEL_DEFAULT;
volatile uint8_t s_alert_effect = BSP_HAPTIC_ALERT_EFFECT_DEFAULT;
volatile bool s_key_feedback = true;

/* 串行化播放/校准，避免后台校准与 UI 触控反馈同时改 GO/MODE。
   播放接口用零超时，正在校准或播放时直接跳过，不阻塞 UI。 */
SemaphoreHandle_t s_op_lock = nullptr;

uint8_t alert_rv_for_level(uint8_t level)
{
    if (level == 0) {
        return 0;
    }
    return (uint8_t)(46 + (uint16_t)(level - 1) * 51 / 9);
}

bool try_play(uint8_t effect, uint8_t rated_reg)
{
    if (!s_ready || effect == 0) {
        return false;
    }
    if (s_op_lock && xSemaphoreTake(s_op_lock, 0) != pdTRUE) {
        return false;
    }
    const bool ok = s_drv.play(effect, rated_reg);
    if (s_op_lock) {
        xSemaphoreGive(s_op_lock);
    }
    return ok;
}

void load_settings()
{
    uint8_t level = BSP_HAPTIC_ALERT_LEVEL_DEFAULT;
    uint8_t effect = BSP_HAPTIC_ALERT_EFFECT_DEFAULT;
    bool key = true;
    bool saved = false;

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, true)) {
        level = prefs.getUChar("alert_lvl", BSP_HAPTIC_ALERT_LEVEL_DEFAULT);
        effect = prefs.getUChar("alert_eff", BSP_HAPTIC_ALERT_EFFECT_DEFAULT);
        key = prefs.getBool("kb_on", true);
        saved = prefs.getBool("ui_saved", false);
        prefs.end();
    }

    if (level > BSP_HAPTIC_ALERT_LEVEL_MAX) {
        level = BSP_HAPTIC_ALERT_LEVEL_MAX;
    }
    if (effect > 123) {
        effect = 0;  /* 非法效果号按“关闭”处理，与原机一致 */
    }

    s_alert_level = level;
    s_alert_effect = effect;
    s_key_feedback = key;

    if (!saved) {
        Preferences out;
        if (out.begin(NVS_NAMESPACE, false)) {
            out.putUChar("alert_lvl", level);
            out.putUChar("alert_eff", effect);
            out.putBool("kb_on", key);
            out.putBool("ui_saved", true);
            out.end();
        }
    }
}

void save_alert_level(uint8_t level)
{
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.putUChar("alert_lvl", level);
        prefs.end();
    }
}

void save_alert_effect(uint8_t effect)
{
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.putUChar("alert_eff", effect);
        prefs.end();
    }
}

void save_key_feedback(bool enabled)
{
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.putBool("kb_on", enabled);
        prefs.end();
    }
}

}  // namespace

bool bsp_haptic_init(void)
{
    if (!s_settings_loaded) {
        load_settings();
        s_settings_loaded = true;
    }
    if (!s_op_lock) {
        s_op_lock = xSemaphoreCreateMutex();
    }
    if (s_ready) {
        return true;
    }

    i2cBusInit();
    s_ready = s_drv.begin();
    if (!s_ready) {
        Serial.println("[BSP] haptic: DRV2605L unavailable; boot feedback disabled");
    }
    return s_ready;
}

bool bsp_haptic_ready(void)
{
    return s_ready;
}

bool bsp_haptic_settings_loaded(void)
{
    return s_settings_loaded;
}

bool bsp_haptic_play(uint8_t effect)
{
    return try_play(effect, BSP_HAPTIC_ALERT_RV);
}

bool bsp_haptic_alert(void)
{
    const uint8_t level = s_alert_level;
    const uint8_t effect = s_alert_effect;
    if (!s_ready || level == 0 || effect == 0) {
        return false;
    }
    return try_play(effect, alert_rv_for_level(level));
}

bool bsp_haptic_preview(uint8_t effect)
{
    const uint8_t level = s_alert_level;
    if (!s_ready || level == 0 || effect == 0) {
        return false;
    }
    return try_play(effect, alert_rv_for_level(level));
}

bool bsp_haptic_key_click(void)
{
    if (!s_ready || !s_key_feedback) {
        return false;
    }
    return try_play(BSP_HAPTIC_EFFECT_KEY_CLICK, BSP_HAPTIC_IDLE_RV);
}

bool bsp_haptic_recalibrate(void)
{
    if (!s_ready) {
        return false;
    }
    if (s_op_lock && xSemaphoreTake(s_op_lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    const bool ok = s_drv.recalibrate();
    if (s_op_lock) {
        xSemaphoreGive(s_op_lock);
    }
    return ok;
}

uint8_t bsp_haptic_get_alert_level(void)
{
    return s_alert_level;
}

bool bsp_haptic_set_alert_level(uint8_t level)
{
    if (level > BSP_HAPTIC_ALERT_LEVEL_MAX) {
        return false;
    }
    s_alert_level = level;
    save_alert_level(level);
    if (level == 0) {
        bsp_haptic_stop();
    }
    return true;
}

uint8_t bsp_haptic_get_alert_effect(void)
{
    return s_alert_effect;
}

bool bsp_haptic_set_alert_effect(uint8_t effect)
{
    if (effect > 123) {
        return false;
    }
    s_alert_effect = effect;
    save_alert_effect(effect);
    if (effect == 0) {
        bsp_haptic_stop();
    }
    return true;
}

bool bsp_haptic_get_key_feedback(void)
{
    return s_key_feedback;
}

void bsp_haptic_set_key_feedback(bool enabled)
{
    s_key_feedback = enabled;
    save_key_feedback(enabled);
    if (!enabled) {
        bsp_haptic_stop();
    }
}

void bsp_haptic_boot_feedback(void)
{
    if (s_boot_done) {
        return;
    }
    s_boot_done = true;

    if (!s_ready) {
        return;
    }

    const uint8_t level = s_alert_level;
    if (level == 0) {
        Serial.println("[BSP] haptic: boot feedback disabled by setting");
        return;
    }

    if (try_play(BSP_HAPTIC_BOOT_EFFECT, alert_rv_for_level(level))) {
        Serial.println("[BSP] haptic: boot feedback");
    } else {
        Serial.println("[BSP] haptic: boot feedback failed");
    }
}

void bsp_haptic_tick(void)
{
    if (!s_ready) {
        return;
    }
    if (s_op_lock && xSemaphoreTake(s_op_lock, 0) != pdTRUE) {
        return;  /* 正在校准或播放，下一轮再收拾 */
    }
    s_drv.poll();
    if (s_op_lock) {
        xSemaphoreGive(s_op_lock);
    }
}

void bsp_haptic_stop(void)
{
    if (!s_ready) {
        return;
    }
    if (s_op_lock && xSemaphoreTake(s_op_lock, 0) != pdTRUE) {
        return;
    }
    s_drv.stop();
    if (s_op_lock) {
        xSemaphoreGive(s_op_lock);
    }
}
