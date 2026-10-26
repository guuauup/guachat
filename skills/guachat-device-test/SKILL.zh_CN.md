---
name: guachat-device-test
description: 在用户明确确认后烧录并在真机上测试 GUACHAT 固件。用户要求上机、抓串口或验证传感器/射频/音频时使用。
---

# GUACHAT 上机测试

## 触发条件

- 用户明确要求烧录或上机测试。
- 需要串口日志定位只有真机才有的问题。
- 需要验证合并镜像能否启动。

## 授权规则

**未经本次会话的明确确认，不得烧录。** 编译成功或插着 USB 都不算授权。
先问，再等答复。

## 步骤

1. 确认目标：
   - 板子型号与 Flash 大小（16 MB）
   - 哪个环境/镜像
   - 是否允许清空 NVS（默认不清）
2. 列出串口：
   ```bash
   pio device list
   ```
3. 带确认烧录：
   ```bash
   python tools/flash.py --env guachat_dev
   # 或
   python tools/flash.py build/guachat-dev-full.bin --port COM9
   ```
   `tools/flash.py` 默认要求输入 `yes`；只有用户已授权这次烧录时才用 `--yes`。
4. 正确抓启动日志：
   - **先打开串口，再复位**
   - 切换 DTR/RTS 复位
   - 读取 15–30 秒
   - 检查是否有 `rst:` 循环和 `[BSP] hardware init done`
5. 按 `docs/testing.md` 的设备清单逐项测试，记录实际读数：
   - 显示/触摸
   - 电源与 TF 卡
   - SHT30/BMP581/IMU/磁力计/RTC
   - LoRa TX（有第二台设备才测 RX）
   - GPS（室外；室内可能无定位）
   - 扬声器蜂鸣与麦克风峰值
6. 报告：
   ```text
   Device tests: ...
   Unverified: ...
   ```
   每个未验证项都要写原因。

## 已知基线事实

- 个别板子 QMC5883P 缺失；先查 I2C0 0x2C，不要先怀疑代码。
- LoRa RX 需要第二台设备，否则标为未验证。
- GPS 室内通常无定位。
- `Serial.setTxTimeoutMs(0)`：没有主机读串口时启动日志会丢。

## 回退

- `docs/testing.md`
- `docs/build-and-flash.md`
- `docs/troubleshooting.md`
