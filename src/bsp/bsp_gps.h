/**
 * bsp_gps.h — MAX-M10S / UART0 GPS
 *
 * 初始化后会在 Core 0 启动 gps 任务：
 *   1. ubx_configure() 自动检测波特率并配置 UBX 输出
 *   2. 持续把 Serial0 字节喂给 ubx 解析器
 * 应用通过 bsp_gps_get() 读取只读快照。
 */
#ifndef BSP_GPS_H
#define BSP_GPS_H

#include "gps/ubx_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 启动 GPS 串口与后台任务（非阻塞，配置在任务里完成） */
bool bsp_gps_init(void);

/** 最近一次解析数据（只读；字段含义见 gps_data_t） */
const gps_data_t *bsp_gps_get(void);

/** 是否已收到过 NAV-PVT */
bool bsp_gps_alive(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_GPS_H */
