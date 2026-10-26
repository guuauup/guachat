# AGENTS.md — GUACHAT AI 开发入口契约

> 这是本项目**唯一强制入口**。任何编码 Agent（Claude Code / Codex / TRAE / Cursor 等）
> 在动手前必须先读完本文件，再按第 10 节的路由表查阅对应文档。
>
> `CLAUDE.md` 只是指向本文件的薄壳；两者冲突时以本文件为准。

---

## 0. 这个仓库是什么

GUACHAT 2G4 是一台基于 **ESP32-S3** 的手持终端硬件：4.0" 触摸屏、LoRa 2.4G、
GPS、IMU/磁力计/气压/温湿度/RTC、TF 卡、I2S 麦克风与功放、电池电量计。

本仓库不是“成品应用仓库”，而是**给 Agent 和二次开发者用的地基**：

- `src/bsp/` 是板级支持层（BSP），封装屏幕、触摸、电源、存储、传感器、LoRa、GPS、音频。
- `[env:guachat_dev]` 是最小开发基线：只初始化 BSP 和一个**硬件自检界面**，可直接编译烧录。
- `main` **不包含应用业务代码**：只保留 BSP、开发基线、文档和工具。新应用从
  `src/dev/` 开始，并整体重写界面。
- 用户最终要做的是**完全不同的新应用**。`src/dev/dev_ui.cpp` 只是 BSP 用法示例，
  必须被重新设计，不允许改个颜色/标题就当作交付。

设备端没有 AI，也没有运行时应用商店。每次开发都是**改源码 → 编译 → 烧录新固件**。

---

## 1. 先做这三件事

1. 读本文件。
2. 读 [`docs/README.md`](docs/README.md)，按任务查索引。
3. 看 `git status`，确认工作区里有没有用户未提交的改动。若当前副本没有
   `.git`（例如解压的源码包），跳过分支操作，但仍然不能覆盖已有文件。

不要在没读文档的情况下猜硬件、猜 API、猜引脚。

### 1.1 必需技能（开发前检查）

本仓库的 Agent 工作流依赖 5 个技能包：

`guachat-setup`、`guachat-develop`、`guachat-build`、`guachat-device-test`、`guachat-debug`。

开始开发前必须确认当前 Agent 能发现并读取这些技能；缺失时自行安装：

```bash
python tools/install_skills.py
python tools/install_skills.py --print
```

其他 Agent 若不支持 `.claude/skills`，把 `skills/<name>/` 复制到它支持的位置，
或直接读 `SKILL.md`。

- 不要只因为目录存在就宣称技能已加载；必须确认当前环境实际可用。
- 安装失败、权限不足或平台不支持时，报告阻塞点和需要用户执行的最小动作。
- 不需要每个任务都调用全部技能；按任务选择匹配的技能。
- 安装/加载技能不代表获得烧录、commit、push 授权。

---

## 2. 硬性规则（违反即返工）

### 2.1 Git

- 先 `git status`。**不得丢弃、覆盖、回滚用户已有的改动**。
- 在新分支开发：`feature/<简短需求名>`；不要直接改 `main`。
- 未经用户明确要求，**不要 commit、不要 push**。
- 不要执行 `git clean -fdx`、`git reset --hard`、`git checkout -- .` 这类会吞掉用户工作的命令。

### 2.2 硬件参数

- 引脚、I2C 地址、总线频率、屏幕参数、射频边界的**唯一代码来源**是
  [`src/bsp/bsp_pins.h`](src/bsp/bsp_pins.h)。
- 应用代码**不得出现裸 GPIO 数字**；需要新引脚时先改 `bsp_pins.h`，再改文档。
- 冲突时的优先级：**实测结果 > `bsp_pins.h` > 驱动实现 > 文档 > README**。
- 拿不准引脚、屏幕方向、传感器型号、按键极性时**必须问用户**，不得猜。

### 2.3 代码边界

| 区域 | 规则 |
| --- | --- |
| `src/bsp/` | 板级底座。可以扩展，但改动会影响所有应用；改前先确认 |
| `src/dev/` | 开发基线与示例。用户应用通常从这里开始，允许整体重写 |
| `src/I2C0/`、`src/I2S/`、`src/gps/`、`src/storage/`、`src/SX1280/` | 驱动层，只提供硬件能力，不放具体界面、页面切换或业务流程。优先通过 BSP 使用；应用差异放在 BSP 或应用层 |

- 应用层只能调用 BSP API，**不要直接** `Wire.beginTransmission`、`SD.begin`、
  `i2s_*`、`radio.*` 或操作 GPIO。绕过 BSP 会破坏总线锁和降级逻辑。
- 新功能的 UI 必须在 `src/dev/`（或你新建的应用目录）里用**纯 LVGL** 创建，
  不依赖任何外部 UI 生成器输出的控件树。

### 2.4 单入口（最容易踩的构建坑）

`main` 只有一个环境 `guachat_dev`，只有一个 `setup()/loop()`：`src/main.cpp`。

新增顶层源码目录默认会被编译。如果新目录也定义了 `setup()/loop()`，会立刻出现
`multiple definition of setup()`。要么把代码并入现有应用目录，要么明确删掉旧入口，
不要靠 `build_src_filter` 长期维持多个互斥环境。

### 2.5 LVGL 与并发

- **所有 `lv_*` 调用只能发生在 LVGL 任务上下文**：
  dev 环境是 `src/main.cpp` 的 `ui_task`。
- 本项目**没有全局 LVGL 锁**。不要从其他任务直接操作控件。
- 其他任务把数据放进队列/缓冲区，由 LVGL 任务轮询；或用 `lv_async_call()`
  （回调仍在 LVGL 任务里执行）。
- `uiTask` 循环里**禁止周期性 `Serial` 打印**：插 USB 时可能阻塞数百毫秒，导致界面卡死。
- I2C 一律走设备 Manager / `i2cBusLock()`；SD 卡访问一律加 `SdLockGuard`。
- Core 0 新增任务优先级 **≤ 6**（LoRa 是 6）；Core 1 新增任务优先级 **≤ 5**（UI 是 5）。

详见 [`docs/concurrency.md`](docs/concurrency.md)。

### 2.6 构建、验证、烧录

- 默认构建环境：`pio run -e guachat_dev`。
- 交付前必须跑完整门禁：`python tools/validate.py --all`。
  它依次执行静态检查、主机测试、构建、合并 `0x0` 镜像，并校验 `full.bin` 与
  `manifest.json` 中的 SHA256/ELF 身份。迭代时可用 `--static` / `--host` 缩短时间。
- **未经用户明确确认，不得烧录**。编译成功、插着 USB、用户说过“可以测试”都不等于授权。
- 烧录前先问；烧录后报告串口日志与设备现象。
- 合并 0x0 固件时**不要强制 `--flash_mode qio`**：PlatformIO 生成的 bootloader 头是 DIO，
  强制 QIO 会导致 `TG0WDT_SYS_RST` 启动循环。使用 `tools/merge_firmware.py`，它会保留原头。

### 2.7 射频与合规（告知，不是禁止）

- 本仓库默认是 **LoRa 2.4G** 配置：2402–2482 MHz，默认/推荐 7 dBm，板载天线。
  模块硬件上限 20 dBm；固件**不做 7 dBm 策略封顶**，高于 7 dBm 只在串口提示风险。
- 用户可以自行修改硬件、天线、功率，也可以启用 WiFi / 蓝牙 / ESP-NOW。
  这些能力**不被代码禁止**，但**合规责任由设备制作者/使用者承担**。
- 实测 LoRa **7 dBm** 可与 WiFi 同时工作；仅验证了该功率。更高功率近距离有
  损坏 ESP32-S3 LNA 的风险，提示后由用户自行决定。并发时建议 LoRa 频率远离
  WiFi 信道；BLE 未做并发实测，按同频段原则处理。
- 交付时必须保留 [`docs/hardware/射频与合规.md`](docs/hardware/射频与合规.md) 中的提示，
  并在报告中说明是否改动了射频参数或引入了无线功能。
- 本项目面向学习与自用，不构成任何型号核准、CE/FCC 或销售合规承诺。

---

## 3. 硬件事实（开发基线）

| 项目 | 值 |
| --- | --- |
| MCU | ESP32-S3-WROOM-1U N16R8，双核 240 MHz，16 MB Flash，8 MB OPI PSRAM |
| 屏幕 | ST7796，320×480，SPI2，BGR，反显；背光 GPIO46（LEDC，安全上限 170/2047） |
| 触摸 | FT6336U，I2C1（SDA 47 / SCL 48，400 kHz），INT 45，RST 由 TCA9535 P15 |
| I2C0 | SDA 2 / SCL 1，100 kHz；TCA9535 0x20、MAX17055 0x36、SHT30 0x44、BMP581 0x47、PCF8563T 0x51、DRV2605L 0x5A、LSM6DS3TRC 0x6A、QMC5883P 0x2C |
| TF 卡 | SDMMC 1 线：CLK 10 / CMD 11 / D0 9，挂载 `/sd` |
| 音频 | I2S0 全双工，默认 8 kHz / 32 bit / 立体声（可运行时改采样率）；BCLK 38、LRC 39、DIN 40、DOUT 41、SD_MODE 42 |
| LoRa | E28-2G4M20S / SX1280，SPI3；MISO 4、MOSI 5、SCK 6、NSS 7、DIO1 8、RXEN 15、TXEN 16、NRESET 17、BUSY 18 |
| GPS | MAX-M10S，UART0，TX 43 / RX 44，230400 |
| 按键 | BOOT GPIO0（低有效） |
| 电池 | 单节 18650，4.4 V / 5200 mAh（开源硬件）；MAX17055 电量计；建议低于 3.40 V 自动关机（基线 3.3 V），关机后 HT7125S 可给 GPS + RTC 备用 >30 天 |

完整引脚与电源树见 [`docs/hardware/硬件连接说明.md`](docs/hardware/硬件连接说明.md)
和 [`docs/hardware/电源树.md`](docs/hardware/电源树.md)。

**实测注意**：个别板子上 QMC5883P 可能无应答（I2C0 扫描不到 0x2C）。
BSP 会以 `mag_ok=false` 降级，这不一定是驱动 bug；先确认硬件，再改代码。

---

## 4. 默认开发流程

1. 明确需求；需求里缺少引脚、外设型号、交互细节时先问。
2. `git status` → 新建 `feature/*` 分支。
3. 读 [`docs/ai-guide.md`](docs/ai-guide.md) 与相关 BSP 文档。
4. 在 `src/dev/` 中实现最小完整功能：
   - 先保证 BSP 初始化与屏幕/触摸可用；
   - UI 用纯 LVGL 重新设计，不复用示例布局；
   - 中文界面必须检查字库覆盖（见 [`docs/fonts.md`](docs/fonts.md)）。
5. 迭代时编译并修复所有警告/错误：`pio run -e guachat_dev`。
6. 交付前跑完整门禁：`python tools/validate.py --all`
   （静态 + 主机 + 构建 + 合并 + manifest/SHA256/ELF 校验）。
7. 完整门禁已包含合并；只需单独合并时：`python tools/merge_firmware.py`。
8. 询问用户是否上机；确认后才烧录并采集串口日志。
9. 按第 6 节格式报告。

---

## 5. UI 要求

- **必须重新设计**，不得照搬 `dev_ui.cpp` 的布局、配色、页签结构后只改文字。
- 屏幕是 320×480 竖屏；触摸是单点电容屏。
- 中文文本需要字库支持。仓库自带 `src/fonts/ui_font_syhtm18.c`（18 px，
  约 6900 个常用汉字，OFL-1.1），用法见 `src/fonts/fonts.h`；
  生僻字或特殊字号仍需生成子集字体，并做覆盖检查（`docs/fonts.md`）。
- 大图片、大数组优先放 PSRAM；不要往 LVGL 内存池里塞大图。
- 动画与定时器在应用退出时必须删除（`lv_timer_del`、`lv_anim_del`、停任务）。

---

## 6. 交付与报告格式

最终回复必须把结果分开写，不允许只说“已完成”：

```text
Build:        PASS / FAIL / NOT RUN（附 RAM/Flash 占用）
Host tests:   PASS / FAIL / NOT RUN（跑了哪些、结果如何）
Device tests: PASS / FAIL / NOT RUN（真机实际验证了什么）
Unverified:   没有条件验证的项，写清原因（例如没有第二台 LoRa 设备）
Artifacts:    合并固件路径、大小、SHA256、manifest.json（若生成）
```

- 没跑就是 `NOT RUN`，不能写“应该没问题”；编译通过只能写 `Build: PASS`。
- 设备测试不由脚本判定 PASS；由执行者按清单记录实际读数。
- 不要把“编译通过”写成“功能正常”。
- 改动射频、分区表、引导配置、WiFi/蓝牙时，必须在报告中单列风险。

---

## 7. 什么时候必须问用户

- 引脚、外设型号、按键极性、屏幕方向不明确。
- 需求会改变电源、射频、分区表或启动流程。
- 需要删除/覆盖用户已有文件或未提交改动。
- 需要烧录、格式化 TF 卡、擦除 Flash。

其余情况自主完成，不要为了确认而反复打断。

---

## 8. 已知坑（先看再动手）

| 现象 | 原因 / 处理 |
| --- | --- |
| `multiple definition of setup()` | 新增目录带了第二个入口；合并进 `src/dev` 或删掉旧入口 |
| 开机 `TG0WDT_SYS_RST` 循环 | 合并固件时强制了 QIO；去掉 `--flash_mode`，保留 bootloader 的 DIO 头 |
| 界面随机死机/断言 | 非 LVGL 任务调用了 `lv_*`；改用队列或 `lv_async_call` |
| 插 USB 后拖动卡顿 | `uiTask` 里在打日志；删掉周期性 Serial |
| I2C 全掉、只能断电 | 绕过 Manager 直接 `Wire`；使用 `i2cBusLock` / 总线恢复 |
| TF 卡偶发崩溃 | 跨核访问没加 `SdLockGuard` |
| 屏幕雪花/条纹 | LoRa 与 TFT 共用 SPI；LoRa 必须在 SPI3 |
| 链接报 DROM 溢出 | 字库/大常量表太大；用子集、放 PSRAM 或改 IROM |
| 中文显示方块 | 字库是子集，缺码点；重新生成字体 |
| `QMC5883P NOT FOUND` | 先查硬件是否焊接/供电；不要先怀疑驱动 |

更多见 [`docs/troubleshooting.md`](docs/troubleshooting.md)。

---

## 9. 常用命令

```bash
# 开发基线
pio run -e guachat_dev
pio run -e guachat_dev -t upload        # 先获得用户确认

# 串口
pio device monitor -b 115200

# 验证
python tools/validate.py --static   # 快速迭代
python tools/validate.py --all      # 完整门禁：静态+主机+构建+合并+产物校验

# 单独合并为可从 0x0 刷写的镜像（完整门禁已包含）
python tools/merge_firmware.py --zip

# Claude Code 技能包（装到 .claude/skills）
python tools/install_skills.py
```

---

## 10. 文档路由表

| 你要做什么 | 先读 |
| --- | --- |
| 了解整体架构、启动顺序、任务模型 | [`docs/architecture.md`](docs/architecture.md) |
| 调用 BSP（屏幕/触摸/电源/SD/传感器/LoRa/GPS/音频） | [`docs/bsp-api.md`](docs/bsp-api.md) |
| 搭环境、编译、生成合并固件、恢复 | [`docs/build-and-flash.md`](docs/build-and-flash.md) |
| 安排任务、避免 LVGL/I2C/SD 并发问题 | [`docs/concurrency.md`](docs/concurrency.md) |
| 做中文界面、生成字体 | [`docs/fonts.md`](docs/fonts.md) |
| 自测、写测试报告 | [`docs/testing.md`](docs/testing.md) |
| 排查启动/显示/总线/射频问题 | [`docs/troubleshooting.md`](docs/troubleshooting.md) |
| 射频参数与合规提示 | [`docs/hardware/射频与合规.md`](docs/hardware/射频与合规.md) |
| 完整需求 → 实现 → 交付的工作流 | [`docs/ai-guide.md`](docs/ai-guide.md) |
| 安装/构建/上机/调试技能包 | [`skills/README.md`](skills/README.md)（`python tools/install_skills.py`） |

---

## 11. 给用户的提示词模板

见根 [`README.md`](README.md) 的“用 AI 开发”一节，或
[`docs/ai-guide.md`](docs/ai-guide.md#提示词模板)。

---

*本文件与源码冲突时以源码和实测为准，并顺手修正本文件。*
