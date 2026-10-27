---
name: guachat-setup
description: 在一台干净电脑上准备 GUACHAT 构建环境。缺少 PlatformIO、Python、工具链或首次构建失败时使用。
---

# GUACHAT 环境准备

## 触发条件

- 新克隆、新机器或 CI。
- 没有 `pio`，或首次构建卡在下载工具链。
- 用户问“怎么构建”。

## 步骤

1. 检查：
   ```bash
   python --version
   pio --version
   git --version
   ```
2. 安装 PlatformIO Core：
   ```bash
   python -m pip install -U platformio
   ```
3. 构建开发基线：
   ```bash
   pio run -e guachat_dev
   ```
   首次会下载 `platform-espressif32 @ 6.5.0`、Arduino 框架、esptool 和 `lib_deps`。
4. 确认入口文件与技能存在：
   ```bash
   ls AGENTS.md docs/README.md src/bsp/bsp_pins.h
   python tools/install_skills.py --check
   ```
   `--check` 不写盘；有 `MISSING` 时用 `python tools/install_skills.py` 安装。
5. 报告：
   - PlatformIO / Python 版本
   - 构建结果与 RAM/Flash 占用
   - 下载了什么
   - 是否缺 USB 驱动

## 注意

- `lib/lvgl` 是仓库内定制的 LVGL 8.3，**不要** `pio pkg update` 或从注册表替换。
- 项目没有固定 `upload_port`，用 `pio device list` 探测。
- Windows 下 ESP32-S3 原生 USB 通常免驱。

## 回退

- `docs/build-and-flash.md`
- `docs/troubleshooting.md`
