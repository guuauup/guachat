/**
 * bsp_radio.h — SX1280 (E28-2G4M20S) 2.4 GHz LoRa 收发
 *
 * 默认参数：2405 MHz / SF10 / BW812.5 / +7 dBm（推荐值）。
 * 频率边界 2402–2482 MHz；功率最高夹到 E28-2G4M20S 的 20 dBm 硬件上限，
 * 高于 7 dBm 时只打印风险提示，不阻止调用方。
 * 自行改硬件/天线或提高功率时，请自行承担射频合规与硬件风险。
 *
 * 注意：rx() 是阻塞调用，不要从 uiTask 或 LVGL 定时器里调用。
 * 非阻塞用法：bsp_radio_available() → bsp_radio_read()。
 */
#ifndef BSP_RADIO_H
#define BSP_RADIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool    ready;
    float   freq_mhz;     /* 当前频率 */
    uint8_t sf;           /* 扩频因子 */
    float   bw_khz;       /* 带宽 */
    int8_t  power_dbm;    /* 模块级功率 */
} bsp_radio_info_t;

/** 初始化 SPI3 + SX1280，进入默认参数与连续接收 */
bool bsp_radio_init(void);

/** 一次性下发频率/扩频/带宽/功率（频率与模块硬件上限由驱动钳位） */
bool bsp_radio_set_params(float freq_mhz, uint8_t sf, float bw_khz, int8_t power_dbm);

/** 进入连续接收（初始化后默认已调用；TX 之后需要重新调用） */
bool bsp_radio_start_rx(void);

/** 阻塞发送；成功返回 true */
bool bsp_radio_send(const uint8_t *data, size_t len);

/** DIO1 是否有待读包（非阻塞） */
bool bsp_radio_available(void);

/** 读取已收到的包；返回 RadioLib 错误码（0=成功） */
int bsp_radio_read(uint8_t *buf, size_t max_len, size_t *out_len);

/** 阻塞接收，timeout_ms 必须显式给出；返回 RadioLib 错误码（0=成功） */
int bsp_radio_receive(uint8_t *buf, size_t max_len, uint32_t timeout_ms,
                      size_t *out_len);

/** 当前频率/扩频/带宽/功率 */
void bsp_radio_info(bsp_radio_info_t *out);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RADIO_H */
