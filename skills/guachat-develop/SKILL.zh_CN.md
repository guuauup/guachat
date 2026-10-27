---
name: guachat-develop
description: 使用 BSP 为 GUACHAT 2G4 开发新应用或界面。用户提出新功能、新屏幕或固件需求时使用。
---

# GUACHAT 开发

## 触发条件

用户要求在本板上新增应用、界面、交互、传感器功能、LoRa 行为，或任何固件改动。

## 先读

1. `AGENTS.md` — 强制契约。
2. `docs/README.md` — 按任务找文档。
3. `docs/bsp-api.md` — 硬件 API。
4. `docs/concurrency.md` — LVGL 与任务规则。

## 流程

1. **只问缺失的信息。** 引脚、传感器型号、屏幕方向、按键极性、需求歧义要问；
   其余自主决定。
2. **保护工作区。**
   ```bash
   git status
   git switch -c feature/<简短名称>
   ```
   不得丢弃用户改动。
3. **设计。**
   - 选 BSP 模块，先读 `src/bsp/` 里的头文件。
   - 界面必须重新设计，不复用 `dev_ui.cpp` 布局。
   - 需要中文时优先用 `src/fonts/fonts.h` 的 `ui_font_syhtm18`；缺字再生成子集字体（`docs/fonts.md`）。
4. **实现。**
   - 初始化顺序从 `src/main.cpp` 开始。
   - 保留 `hw_init_task` 里的开机反馈：`bsp_haptic_init()` +
     `bsp_haptic_boot_feedback()`。本机没有电源指示灯，一次震动用于提示
     固件已经启动；`loop()` 里的 `bsp_haptic_tick()` 也要保留。
     强度设为 0 时会同时关闭提醒和开机震动。
   - 按钮/滑块/roller/开关的事件回调里调用 `bsp_haptic_key_click()` 做低压触控反馈；
     用户设置用 `bsp_haptic_get/set_alert_*()` 和 `bsp_haptic_get/set_key_feedback()`。
   - `bsp_haptic_recalibrate()` 阻塞 1–3 秒，只能放后台任务，不要在 LVGL 回调里调用。
   - 用新界面整体替换 `src/dev/dev_ui.cpp`。
   - 所有 `lv_*` 调用留在 `ui_task` 或 LVGL 回调里。
   - 后台工作放独立任务，通过队列/快照交给 UI。
   - 界面用纯 LVGL 自建，不要复制外部生成器代码。
5. **构建与验证。**
   快速迭代：
   ```bash
   pio run -e guachat_dev
   python tools/validate.py --static
   ```
   交付前跑完整门禁：
   ```bash
   python tools/validate.py --all
   ```
   它会执行静态检查、主机测试、构建、0x0 合并，核对 `full.bin` 与
   `manifest.json` 中的 SHA256/ELF 身份，并运行 `tools/verify_firmware.py`
   独立解析镜像（芯片/Flash、DIO 头、分区 MD5、应用描述符、ELF 身份）。
   需要归档时：
   ```bash
   python tools/archive_firmware.py create --env guachat_dev
   python tools/archive_firmware.py verify dist/firmware/<sha256>
   ```
6. **报告**：`Build / Host tests / Device tests / Unverified / Artifacts`。
   状态用 `PASS / FAIL / NOT RUN`；编译成功只能写 `Build: PASS`。
7. **烧录前先问。** 编译成功或插着 USB 都不等于授权。

## 硬性规则

- 硬件常量只在 `src/bsp/bsp_pins.h`；不写裸 GPIO。
- 不要删除开机的一次震动，除非你提供了等效的开机提示；只用
  `bsp_haptic_*`，不要直接操作 DRV2605L 寄存器。
- 不要在 UI 任务里调用 `bsp_haptic_recalibrate()`；它会阻塞 1–3 秒。
- 应用代码不直接调用 `Wire`、`SPI`、`SD.begin`、`i2s_*`、`radio.*`。
- `main` 只有 BSP 与开发基线，界面用纯 LVGL 写。
- LVGL 没有全局锁，只有 UI 任务能调用。
- 新任务：Core 0 优先级 ≤ 6，Core 1 优先级 ≤ 5。
- UI 任务循环里禁止周期 `Serial` 输出。
- 合并固件不要强制 `--flash_mode qio`。

## 回退

- 构建报错：`docs/troubleshooting.md`
- 字体问题：`docs/fonts.md`
- 烧录/合并：`docs/build-and-flash.md`
- 设备测试：`docs/testing.md`
