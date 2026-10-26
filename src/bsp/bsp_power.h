/**
 * bsp_power.h — 电池电压 / 电流 / 电量（MAX17055）
 */
#ifndef BSP_POWER_H
#define BSP_POWER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     valid;
    uint16_t voltage_mv;
    int16_t  current_ma;    /* 正=充电，负=放电 */
    uint8_t  soc_percent;   /* 0..100 */
    uint16_t remaining_mah;
    uint16_t design_mah;
    float    temperature_c;
    bool     charging;      /* current_ma > 50 */
} bsp_power_info_t;

/** 初始化 MAX17055（需先初始化 I2C0；TCA9535 begin 已完成总线初始化） */
bool bsp_power_init(void);

/** 读一次电池数据；成功返回 true */
bool bsp_power_read(bsp_power_info_t *out);

#ifdef __cplusplus
}
#endif

#endif /* BSP_POWER_H */
