/**
 * I2C0 总线管理 — 全局互斥锁 + 运行时总线恢复
 *
 * 背景: I2C0 (Wire) 上挂了多个设备, 且可能被多个 FreeRTOS 任务并发访问
 *   (当前基线是 hw_init_task 初始化 + ui_task 轮询; 用户新增后台任务后同样适用)。
 *   高优先级任务会抢占低优先级任务正在进行的多字节 I2C 传输, 从机看到
 *   乱序时序后可能把 SDA 拉死在低电平, 导致后续所有 I2C 事务超时、系统卡死。
 *
 *   - i2cBusLock/Unlock: 递归互斥锁, 包围每次 I2C 事务, 保证同一时刻只有一个
 *     任务在用总线 (递归以支持同一任务内嵌套读写, 如 MAX17055 setEmptyVoltage)。
 *   - i2cBusReportError/Success: 统计事务成败; 连续失败达阈值时, 在持锁状态下
 *     执行总线恢复 (翻转 SCL 释放 SDA + 重新初始化 Wire), 避免"卡死必须断电"。
 */
#ifndef I2C0_I2CBUS_H
#define I2C0_I2CBUS_H

#include <Arduino.h>

// 引脚唯一来源: src/bsp/bsp_pins.h
#include "bsp/bsp_pins.h"
#define I2C0_SCL_PIN  BSP_I2C0_SCL
#define I2C0_SDA_PIN  BSP_I2C0_SDA

/** 初始化 I2C 总线锁 (Wire.begin 之前调用一次即可, 幂等) */
void i2cBusInit();

/** 递归互斥锁: 包围每一次 I2C 事务 (可重入, 支持同任务内嵌套读写) */
void i2cBusLock();
void i2cBusUnlock();

/** 非阻塞尝试加锁: 总线空闲则加锁返回 true; 被占用(SHT30 15ms 时钟拉伸等)则立即返回 false。
 *  供 UI/LVGL 等不能阻塞的路径使用。 */
bool i2cBusTryLock();

/** 上报一次 I2C 事务失败: 连续失败达阈值 → 在持锁状态下恢复总线 (带 500ms 冷却) */
void i2cBusReportError();
/** 上报一次 I2C 事务成功: 清零连续失败计数 */
void i2cBusReportSuccess();

/** 总线恢复: 检测 SDA 拉死则翻转 SCL 释放, 并 (重新)初始化 Wire (开机/运行时通用) */
void i2cBusRecoverNow();

#endif
