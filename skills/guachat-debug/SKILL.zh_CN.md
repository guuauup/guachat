---
name: guachat-debug
description: 诊断 GUACHAT 启动循环、panic、花屏、I2C/SD 故障与射频/音频问题。设备异常或构建后启动不正确时使用。
---

# GUACHAT 排障

## 触发条件

- 重启循环、`TG0WDT_SYS_RST`、Guru Meditation、panic。
- 黑屏、雪花、条纹、触摸无反应。
- I2C 设备消失、SD 卡崩溃、LoRa/音频失败。
- 合并镜像与 `pio upload` 行为不同。

## 第一步：拿到启动日志

1. **先打开串口**（固件使用 `Serial.setTxTimeoutMs(0)`）。
2. 切换 DTR/RTS 复位。
3. 抓 15–30 秒。
4. 重点看：
   - `rst:0x...` 与 `mode:DIO` / `mode:QIO`
   - `Saved PC:`
   - 最后一条 `[BSP] ...`
   - 是否到 `[BSP] hardware init done`

## 判断表

| 现象 | 可能原因 | 处理 |
| --- | --- | --- |
| 只打印 ROM 日志，无应用 | 合并固件强制了 QIO | 去掉 `--flash_mode` 重新合并 |
| 改代码后启动循环 | 初始化/任务崩溃 | 看 PC，逐步二分初始化顺序 |
| 黑屏但串口正常 | 背光/TCA9535/LVGL 缓冲 | 查日志、`bsp_backlight_set` |
| 雪花/条纹 | LoRa 用了 SPI2 | 改回 SPI3 |
| LVGL 随机断言 | 非 UI 任务调 `lv_*` | 队列 / `lv_async_call` |
| 插 USB 后界面卡顿 | `uiTask` 周期 Serial | 删除日志 |
| I2C 全部失效 | 直接 Wire / 总线卡死 | 走 Manager / `i2cBusLock` |
| TF 卡崩溃 | 没加 `SdLockGuard` | 文件访问加锁 |
| `QMC5883P NOT FOUND` | 硬件缺失/地址不对 | 扫描 I2C，查供电/焊接 |
| BMP581 显示 10.2 hPa | hPa/Pa 混用 | BSP 应乘 100 |
| LoRa 收不到 | RX 没重新武装 | 读包/发完后 `bsp_radio_start_rx()` |

## 规则

- 不要用“删检查/关看门狗”来掩盖问题。
- 未经用户要求不要改射频功率/频率；改了必须报告。
- 保持最小复现，一次只改一个变量。

## 回退

- `docs/troubleshooting.md`
- `docs/concurrency.md`
- `docs/build-and-flash.md`
- `docs/testing.md`
