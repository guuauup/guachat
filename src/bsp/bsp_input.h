/**
 * bsp_input.h — 触摸输入的板级 API
 */
#ifndef BSP_INPUT_H
#define BSP_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 复位并初始化 FT6336U，注册 LVGL 指针输入设备（需先 lv_init()） */
void bsp_input_init(void);

/** 触摸是否可用 */
bool bsp_touch_available(void);

/** 最后一次触摸事件的 millis() 时间戳 */
uint32_t bsp_touch_last_activity_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_INPUT_H */
