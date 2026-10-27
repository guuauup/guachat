/**
 * bsp_haptic.h — DRV2605L 线性马达（LRA）
 *
 * 开机默认震动一次，用于在没有电源指示灯时提示用户设备已经启动。
 * 应用可以替换界面，但除非提供等效的开机提示，否则不要删除
 * bsp_haptic_boot_feedback()。
 *
 * 提醒强度、提醒效果和触控反馈保存在 NVS 命名空间 "drv2605"；
 * 强度为 0 时会同时关闭提醒和开机震动。
 */
#ifndef BSP_HAPTIC_H
#define BSP_HAPTIC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DRV2605L ROM Library 6（LRA）常用效果号，完整表见数据手册 §11.2 */
#define BSP_HAPTIC_EFFECT_STRONG_CLICK_100  1
#define BSP_HAPTIC_EFFECT_SHARP_CLICK_100   4
#define BSP_HAPTIC_EFFECT_DOUBLE_CLICK_100  10
#define BSP_HAPTIC_EFFECT_TRIPLE_CLICK_100  12
#define BSP_HAPTIC_EFFECT_STRONG_BUZZ_100   14
#define BSP_HAPTIC_EFFECT_ALERT_1000MS      16
#define BSP_HAPTIC_EFFECT_LONG_DOUBLE_100   44
#define BSP_HAPTIC_EFFECT_BUZZ_2_80         48
#define BSP_HAPTIC_EFFECT_KEY_CLICK         26  /* Sharp Tick 3 60%，触控反馈用 */

/* 提醒强度：0 = 关闭，1..10 映射到 RV 46..97（约 1.0–2.1 V） */
#define BSP_HAPTIC_ALERT_LEVEL_MIN          0
#define BSP_HAPTIC_ALERT_LEVEL_MAX          10
#define BSP_HAPTIC_ALERT_LEVEL_DEFAULT      5
#define BSP_HAPTIC_ALERT_EFFECT_DEFAULT     BSP_HAPTIC_EFFECT_BUZZ_2_80

/** 初始化 DRV2605L（要求 I2C0 已初始化）；失败返回 false，设备仍可继续运行 */
bool bsp_haptic_init(void);

/** 驱动是否可用 */
bool bsp_haptic_ready(void);

/** NVS 中的震动设置是否已经加载（UI 可据此同步控件初值） */
bool bsp_haptic_settings_loaded(void);

/**
 * 播放一个 ROM 效果（固定使用 BSP_HAPTIC_ALERT_RV，非阻塞）。
 * 不读取提醒强度/效果设置；需要按用户设置播放时用 bsp_haptic_alert()。
 */
bool bsp_haptic_play(uint8_t effect);

/** 按当前提醒强度播放当前提醒效果；强度或效果为 0 时不播放。 */
bool bsp_haptic_alert(void);

/** 按当前提醒强度试听指定效果；强度为 0 时不播放。 */
bool bsp_haptic_preview(uint8_t effect);

/** 低压触控反馈（Sharp Tick 3 60%）；用户关闭触控反馈时静默返回 false。 */
bool bsp_haptic_key_click(void);

/**
 * 重新校准 LRA（阻塞 1–3 秒，马达会嗡鸣）。
 * 必须在后台任务里调用；UI 回调里调用会卡住 LVGL。
 */
bool bsp_haptic_recalibrate(void);

/** 提醒强度 0..10（0 = 关闭），默认 5。 */
uint8_t bsp_haptic_get_alert_level(void);
bool    bsp_haptic_set_alert_level(uint8_t level);

/** 提醒效果号（0 = 关闭），默认 48（Buzz 2 80%）。 */
uint8_t bsp_haptic_get_alert_effect(void);
bool    bsp_haptic_set_alert_effect(uint8_t effect);

/** 触控反馈开关，默认开。 */
bool bsp_haptic_get_key_feedback(void);
void bsp_haptic_set_key_feedback(bool enabled);

/** 开机提示：震动一次（Strong Click）。每次上电只执行一次，失败静默。 */
void bsp_haptic_boot_feedback(void);

/** 周期调用（建议 100–500 ms）：结束播放后回到 standby。 */
void bsp_haptic_tick(void);

/** 立即停止并进入 standby */
void bsp_haptic_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_HAPTIC_H */
