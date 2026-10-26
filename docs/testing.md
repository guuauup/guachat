# 测试与交付报告

> 编译通过 ≠ 功能正常。本项目把验证分成四层：
> **静态检查 → 主机测试 → 设备测试 → 未验证项**。
> 报告必须分开写。

---

## 1. 静态检查

### 1.1 构建

```bash
pio run -e guachat_dev
```

必须 0 error。新增 warning 要处理或说明。

### 1.2 项目检查脚本

```bash
python tools/validate.py --static
```

至少覆盖：

| 检查 | 期望 |
| --- | --- |
| 入口文件、BSP、工具存在 | 通过 |
| `main` 不含应用业务代码 | 通过 |
| 只有一个 `guachat_dev` 环境和一个入口 | 通过 |
| 应用代码出现裸 GPIO / 直接 `Wire` | 警告或失败（按规则） |
| 文档内相对链接 | 不悬空 |
| 中文字符串字体覆盖 | 缺字失败 |
| 射频参数越界 | 警告（默认 2402–2482 MHz、推荐 7 dBm、硬件上限 20 dBm；用户改动自行负责） |
| WiFi / BT / ESP-NOW 符号 | **只警告，不失败**（用户自担合规责任） |

> 射频与无线的检查是**提示**，不是禁用。用户可以在自己的设备上启用，
> 但报告中必须写明。

### 1.3 合规/危险操作 grep（人工）

```bash
grep -rln "esp_now\|WiFi\.h\|esp_wifi\|WebServer\|HTTPClient\|BLEDevice\.h\|BLE\.h\|esp_bt\|esp_ble" src/ | wc -l
```

有输出不代表失败；确认是否是有意引入，并在报告中说明。

---

## 2. 主机测试

纯逻辑（协议编解码、数学运算、单位换算、状态机）应能在 PC 上测试，不依赖硬件。

```bash
python tools/validate.py --host
# 或
python -m unittest discover -s tools/host_tests -p "test_*.py" -v
```

当前基线有 9 个仓库契约测试（合并偏移、Flash 大小、单环境、射频边界、入口文档）；
纯逻辑测试用同样的 `unittest` 目录扩展。没有主机测试时，报告里写 `Host tests: none`。

---

## 3. 设备测试

### 3.1 前提

- 已获得用户烧录授权。
- 串口可用（先开串口再复位，见 [`build-and-flash.md`](build-and-flash.md)）。
- 知道设备当前烧的是哪个环境。

### 3.2 清单（`guachat_dev` 基线）

> 下表针对仓库自带的五页签硬件自检界面。若已替换成新应用，硬件项仍然适用，
> 显示/触摸项改为验证新界面的布局、点击命中和状态变化，并补测应用自身的功能。

逐项记录“实际现象/读数”，不要只打勾。

| # | 项目 | 操作 | 通过标准 | 实测 |
| --- | --- | --- | --- | --- |
| 1 | 启动 | 上电/复位 | 串口完整打印到 `[BSP] hardware init done`，无重启循环 | |
| 2 | 显示 | 观察屏幕 | 五个页签正常显示，无花屏/雪花 | |
| 3 | 触摸 | 点击页签/按钮 | 切换、按钮响应正常 | |
| 4 | 电源 | Power 页 | 电压约 3.3–4.4 V，SOC 合理，无 `not detected` | |
| 5 | TF 卡 | Power 页 | `mounted at /sd`，容量/剩余合理 | |
| 6 | 温湿度 | Sensors 页 | SHT30 温湿度合理，露点 ≤ 温度 | |
| 7 | 气压 | Sensors 页 | BMP581 约 950–1050 hPa（随海拔/天气变化） | |
| 8 | IMU | Sensors 页 | 静止时 accel 约 (0,0,9.8)，晃动有变化 | |
| 9 | 地磁 | Sensors 页 | 有磁力计时 `mag_ok`，转动设备航向变化 | |
| 10 | RTC | Sensors 页 | 时间在走，VL 不报 LOW | |
| 11 | LoRa TX | LoRa 页勾选 `持续发送` | 每 500 ms 发一包，TX 计数递增；取消勾选后停止发包 | |
| 12 | LoRa RX | 第二台设备发送 | RX 计数增加，Last packet 更新 | |
| 13 | GPS | GPS 页 | 室外能看到卫星/定位；室内允许无定位 | |
| 14 | 扬声器 | Audio 页 `BEEP` | 能听到提示音，音量滑块有效 | |
| 15 | 麦克风 | Audio 页 | Mic peak 随环境声音变化 | |
| 16 | LoRa + WiFi 并发 | LoRa 7 dBm，LoRa 频率远离 WiFi 信道 | 两者同时工作；记录丢包率/吞吐；高于 7 dBm 仅串口提示，风险自行评估 | |

### 3.3 已知的基线结果（2026-10-26）

本机实测：

- 显示、触摸、TCA9535、MAX17055、TF 卡、SHT30、BMP581、PCF8563T、
  LSM6DS3TRC、SX1280 初始化均成功。
- **QMC5883P 无应答**（I2C0 扫描不到 0x2C），BSP 已降级；需确认该板硬件。
- **LoRa 只有一台设备**：只验证了初始化与 TX 返回成功，RX 标为 `Unverified`。
- GPS 室内无定位，属于环境限制。
- BMP581 单位问题已修复（现在约 1010 hPa）。
- 合并镜像 `build/guachat-dev-full.bin` 已从 `0x0` 实机烧录：启动日志显示
  `mode:DIO`，走到 `[BSP] hardware init done`，无重启循环。
- 自动检查：`tools/validate.py --static` 12 项通过；`tools/validate.py --host`
  9 项通过。

---

## 4. 报告格式

```text
Build:
  pio run -e guachat_dev: SUCCESS
  RAM 8.2% / Flash 11.7% / DROM 21.30%

Host tests:
  tools/host_tests: 9 passed

Device tests:
  启动：通过，无重启循环
  显示/触摸：通过
  电源：4.05 V / 78% / -120 mA
  TF：挂载 /sd，843 MB 可用
  传感器：SHT30 32.5°C 45.7%RH；BMP581 1010.2 hPa；IMU 正常；RTC 正常
  LoRa TX：返回成功
  音频：BEEP 有声；Mic peak 随说话变化

Unverified:
  LoRa RX：没有第二台设备
  QMC5883P：该板 I2C0 无应答，需硬件确认
  GPS 定位：室内无信号

Artifacts:
  build/guachat-dev-full.bin
  size 738288 bytes
  sha256 ...
```

### 4.1 写作规则

- 没有实测的项，不要写“正常”。
- 失败项要写现象、日志关键行、初步判断。
- 改动过射频/分区/启动配置时，单列“风险”段落。
- 报告是给用户看的，不是给脚本看的；避免只贴日志。

---

## 5. 什么时候可以判定“完成”

- [ ] `pio run -e guachat_dev` 成功
- [ ] 静态检查通过（警告需解释）
- [ ] 设备测试清单中与需求相关的项全部实测
- [ ] 未验证项明确列出原因
- [ ] 合并固件已生成并校验 SHA256（若任务要求交付固件）
- [ ] 未混入应用业务代码或第二个 `setup()`
- [ ] 未擅自烧录（烧录已获授权）
