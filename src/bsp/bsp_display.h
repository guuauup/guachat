/**
 * bsp_display.h — 显示与背光的板级 API
 *
 * 线程约束：只允许在初始化阶段或 LVGL 任务上下文调用；
 * flush 回调由 LVGL 在 lv_timer_handler() 所在任务中执行。
 */
#ifndef BSP_DISPLAY_H
#define BSP_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 TFT 硬件、复位与背光（LVGL 之前调用） */
void bsp_display_init(void);

/** 分配 LVGL 缓冲并注册显示驱动（需先 lv_init()） */
void bsp_display_lvgl_init(void);

/** 背光亮度 0-100%（内部按安全上限钳位） */
void bsp_backlight_set(uint8_t percent);

/** 背光占空比 0-2047（内部钳位到安全上限） */
void bsp_backlight_set_duty(uint16_t duty);

/** 背光开/关（不清除亮度设置） */
void bsp_backlight_on(bool on);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DISPLAY_H */
