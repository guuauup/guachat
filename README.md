# GUACHAT AI

> **面向 AI 二次开发的 ESP32-S3 手持硬件底座。**
> 仓库只保留硬件抽象层（BSP）、最小可运行界面和让 Agent 能独立完成开发的文档/工具。

这是一块 **可以直接交给编码 Agent 开发** 的硬件：你描述需求，Agent 读契约和 BSP，
写出全新的固件，最后交付可从 `0x0` 刷写的合并镜像。

- 开发基线：`[env:guachat_dev]` + `src/dev/`
- 入口契约：[AGENTS.md](AGENTS.md)
- 文档索引：[docs/README.md](docs/README.md)

---

## 1. 仓库给了什么

| 资产 | 位置 | 说明 |
| --- | --- | --- |
| BSP | [src/bsp/](src/bsp/) | 屏幕、触摸、电源、TF 卡、传感器、LoRa、GPS、音频的稳定 API |
| 硬件常量 | [src/bsp/bsp_pins.h](src/bsp/bsp_pins.h) | **引脚/地址的唯一来源**，不要猜 |
| 最小应用 | [src/dev/](src/dev/) | 初始化骨架 + 五页签硬件自检界面，可整体替换 |
| 文档 | [docs/](docs/) | 架构、BSP API、并发、字体、构建、测试、排障 |
| 工具 | [tools/](tools/) | 静态检查、主机测试、合并固件、字体检查 |
| Skills | [skills/](skills/) | 给 Agent 按需加载的工作流 |

设备端没有 AI 运行时。Agent 的产出是**新固件**，需要重新编译和烧录。

---

## 2. 快速开始

```bash
pio run -e guachat_dev                 # 编译开发基线
pio run -e guachat_dev -t upload       # 烧录（先确认串口和设备）
pio device monitor -b 115200           # 串口日志
python tools/install_skills.py         # 给 Claude Code 安装 skills/
python tools/validate.py --all         # 完整门禁：静态+主机+构建+合并+产物校验
```

合并为可从 `0x0` 刷写的镜像：

```bash
python tools/merge_firmware.py --zip
# build/guachat-dev-full.bin + .sha256 + manifest.json + restore.zip
```

烧录与合并注意事项见 [docs/build-and-flash.md](docs/build-and-flash.md)。
**合并时保留 bootloader 自带的 DIO 头，不要强制 QIO。**

---

## 3. 主要硬件

| 类别 | 型号 |
| --- | --- |
| 主控 | ESP32-S3-WROOM-1U（N16R8），16 MB Flash + 8 MB OPI PSRAM |
| 显示/触摸 | 4.0″ IPS 320×480，ST7796 + FT6336U |
| LoRa | E28-2G4M20S（SX1280），2402–2482 MHz，SPI3 |
| 气压/温湿度 | BMP581 + SHT30 |
| 磁力计 / IMU | QMC5883P / LSM6DS3TRC |
| GPS | u-blox MAX-M10S |
| 电源 | TPS63802 + MAX17055，单节 18650（4.4 V / 5200 mAh，开源硬件）；建议低于 3.40 V 自动关机（基线 3.3 V），HT7125S 可为 GPS+RTC 备用 >30 天 |
| RTC / 触觉 | PCF8563T / DRV2605L |
| 音频 | I2S 麦克风 + MAX98357 |
| 存储 | TF 卡（SDMMC） |

完整引脚、I2C 地址、SPI 分配以 `src/bsp/bsp_pins.h` 为准。

---

## 4. 用 AI 开发

把仓库和需求交给 Claude Code / Codex / Cursor 等 Agent，使用下面的提示词：

```text
请基于完整项目 <仓库 URL> 开发：<你的需求>。
如果有本地 checkout 就直接复用。

先读 AGENTS.md 和 docs/README.md，再按索引查阅硬件接口、BSP API 和示例。
准备环境并安装项目要求的 Skills。

保留现有改动，在新分支开发；复用 BSP，重新设计应用界面，不要照搬示例；
中文界面检查字库覆盖。关键需求不明确时先问我，其余自主完成。

运行项目验证，交付可从 0x0 刷写的合并固件；分开报告
Build / Host tests / Device tests / Unverified。

完成后主动询问是否上机测试，未经确认不要烧录。
```

工作流说明见 [docs/ai-guide.md](docs/ai-guide.md)。

### 开发规则（摘要）

- 硬件参数以 `bsp_pins.h` 为准，不猜引脚。
- 所有 `lv_*` 只在 LVGL 任务里调用；本项目没有全局 LVGL 锁。
- 界面直接用 LVGL 写，不使用外部 UI 生成器。
- 未经用户确认不烧录；合并固件不要强制 QIO。
- 不要在后台任务里直接改控件，也不要周期 `Serial.print` 拖慢 UI。

完整规则见 [AGENTS.md](AGENTS.md) 和 [docs/concurrency.md](docs/concurrency.md)。

---

## 5. 射频与合规

这块板子使用 2.4 GHz LoRa。**你可以自行修改射频参数，也可以启用 WiFi/蓝牙**，
但需要自行确认并承担当地法规（SRRC / CE / FCC 等）要求的责任。
实测 LoRa **7 dBm** 可与 WiFi 同时工作，这是目前唯一验证过的并发功率。
固件不把 7 dBm 作为硬限制：默认/推荐 7 dBm，可自行上调到模块硬件上限 20 dBm，
超过 7 dBm 时串口只提示风险。更高功率近距离有损坏 ESP32-S3 LNA 的风险，
尤其在与 WiFi/BLE 并发时；并发时请让 LoRa 频率远离 WiFi 信道，风险自担。

仓库默认参数（2402–2482 MHz、默认 7 dBm、板载天线）是保守起点，
不是对任何地区合规性的保证。详见
[docs/hardware/射频与合规.md](docs/hardware/射频与合规.md)。

---

## 6. 许可与第三方

本项目以 **GNU General Public License v3.0（或更高版本）** 发布，版权归 **guaup** 所有。
分发修改版本时须同样以 GPL 提供完整对应源码；完整条款见 [LICENSE](LICENSE)。

第三方组件（LVGL、RadioLib、TFT_eSPI 等）的许可证、版权归属和发布二进制
固件时的附加义务，见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)；
许可证全文位于 [third_party/licenses/](third_party/licenses/)。
