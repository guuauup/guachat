/**
 * bsp_buttons.h — 三个可编程实体按键
 *
 * 可编程（均低有效）:
 *   BOOT / 锁屏   GPIO0，内部上拉
 *   音量减        TCA9535 P00，外部上拉
 *   音量加        TCA9535 P01，外部上拉
 *
 * 开关机键不在 S3 的 GPIO 上，是纯硬件电源锁存，固件不能读取。
 */
#ifndef BSP_BUTTONS_H
#define BSP_BUTTONS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BSP_BUTTON_BOOT = 0,
    BSP_BUTTON_VOL_UP,
    BSP_BUTTON_VOL_DOWN,
    BSP_BUTTON_COUNT
} bsp_button_t;

typedef struct {
    bool boot;          /* 总是有效 */
    bool vol_up;        /* vol_valid 为 true 时有效 */
    bool vol_down;      /* vol_valid 为 true 时有效 */
    bool vol_valid;     /* 本次是否成功读到 TCA9535 的音量键 */
} bsp_buttons_state_t;

/** 初始化 GPIO0 为上拉输入（TCA9535 需先 begin）。可重复调用。 */
void bsp_buttons_init(void);

/**
 * 读取三个按键的原始状态（true = 按下）。
 *
 * 非阻塞：I2C0 被占用时 vol_valid = false，此时音量键字段无效；
 * boot 始终有效。应用应自行消抖（建议 20–50 ms 轮询）。
 *
 * @return vol_valid；false 表示音量键本次未读到。
 */
bool bsp_buttons_read(bsp_buttons_state_t *out);

/**
 * 读取单个按键。
 *
 * true = 按下。音量键读取失败时返回 false（与“未按下”不可区分），
 * 周期轮询请优先使用 bsp_buttons_read()。
 */
bool bsp_button_pressed(bsp_button_t button);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BUTTONS_H */
