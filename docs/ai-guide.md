# AI 开发工作流

> 面向使用编码 Agent 的开发者。目标是让 Agent 在**只读仓库 + 一块开发板**的条件下，
> 把你的需求变成可刷写的新固件，并且能自己判断“做完了没有”。

---

## 1. 这个仓库如何支撑 Agent

| 机制 | 位置 | 作用 |
| --- | --- | --- |
| 入口契约 | [`AGENTS.md`](../AGENTS.md) | 规定边界、禁止项、报告格式 |
| 文档索引 | [`README.md`](README.md) | 按问题取文档，不必通读 |
| BSP | `src/bsp/` | 屏幕/触摸/电源/SD/传感器/LoRa/GPS/音频/触觉的稳定 API |
| 最小基线 | `[env:guachat_dev]` + `src/dev/` | 可直接编译烧录的起点和用法示例 |
| 验证工具 | `tools/` | 静态检查、编译、合并固件、字体检查 |
| Skills | `skills/` | 把重复工作流打包给 Agent 按需加载 |

设备端没有 AI 运行时。Agent 产出的是**新固件**，需要重新编译和烧录。

---

## 2. 一次完整开发

### 2.1 需求

Agent 应先把需求拆成：

- 要用的硬件（屏幕？传感器？LoRa？音频？SD？）
- 交互流程（几个页面、按钮、手势）
- 数据来源与刷新频率
- 是否需要中文
- 是否改动射频/电源/分区

缺少引脚、型号、极性、屏幕方向时，Agent **必须问**；其余细节自主决定。

### 2.2 分支与边界

```bash
git status
git switch -c feature/<short-name>
python tools/install_skills.py          # Claude Code：加载 skills/
```

- 保留用户未提交的改动。
- 不要引入外部 UI 生成器输出或应用业务代码。
- 硬件常量只改 `src/bsp/bsp_pins.h`。

### 2.3 实现

推荐顺序：

1. 在 `src/main.cpp` 保留初始化骨架（Serial → TCA9535 → buttons → display → LVGL → input → UI → 背光 → 任务）。
2. 保留 `hw_init_task` 里的开机反馈：`bsp_haptic_init()` +
   `bsp_haptic_boot_feedback()`，以及 `loop()` 里的 `bsp_haptic_tick()`。
   本机没有电源指示灯，一次震动用于告诉用户固件已经启动；除非提供等效提示，
   否则不要删除。用户把强度设为 0 时开机震动会自动关闭。
   新界面的按钮/滑块触控反馈调用 `bsp_haptic_key_click()`；
   重新校准 `bsp_haptic_recalibrate()` 阻塞 1–3 秒，只能放后台任务。
3. 把 `src/dev/dev_ui.cpp` 整体替换成新界面；不要保留页签布局后只换文字。
   三个可编程实体按键（BOOT/锁屏、音量加、音量减）用
   `bsp_buttons.h` 读取，可实现翻页、快捷键、组合键等；开关机键不可编程。
4. 需要新外设时，先看 `src/bsp/*.h` 和 `docs/bsp-api.md`；确需新驱动时放在 `src/` 下并走 BSP 封装。
5. 所有 LVGL 操作留在 `ui_task`；后台任务通过队列/快照交给 UI。
6. 中文界面：优先用 `src/fonts/fonts.h` 里的 `ui_font_syhtm18`，设置
   `lv_obj_set_style_text_font()`；缺字时再生成子集字体并做覆盖检查。

### 2.4 构建与验证

交付前跑完整门禁：

```bash
python tools/validate.py --all
```

它依次执行静态检查、主机测试、构建、合并 `0x0` 镜像，并用
`tools/verify_firmware.py` 独立校验镜像结构、分区 MD5、应用描述符和 ELF 身份。
迭代时可以先用：

```bash
pio run -e guachat_dev
python tools/validate.py --static
python tools/validate.py --host
python tools/verify_firmware.py         # 已有构建时单独复核
```

必须处理编译错误与新增警告。资源占用要记录：

- dev 基线约 RAM 8%、Flash 4%，余量很大；
- 大字体/大数组优先放 PSRAM 或 IROM，不要盲目塞进 LVGL 池；
- DROM 上限约 8 MB，字体与常量表是主要增长点。

### 2.5 合并固件

完整门禁已包含合并；只需单独合并时：

```bash
python tools/merge_firmware.py
```

产物：

```text
build/guachat-dev-full.bin
build/guachat-dev-full.bin.sha256
build/guachat-dev-full.manifest.json
```

`manifest.json` 记录 merged image、application、bootloader、partitions、
boot_app0、ELF、MAP 的路径、大小与 SHA256，用于刷机和崩溃符号匹配。

合并后必须通过独立校验（`merge_firmware.py` 默认已调用）：

```bash
python tools/verify_firmware.py
```

需要交付或存档时，生成以镜像 SHA256 命名的归档：

```bash
python tools/archive_firmware.py create --env guachat_dev
python tools/archive_firmware.py verify dist/firmware/<sha256>
```

归档包含镜像、校验和、manifest、ELF、MAP 与 `archive.json`；`dist/` 不入库。
注意：从 `0x0` 刷 `full.bin` 不保证清空 NVS（设置/校准）；需要保留时改用
`pio run -e guachat_dev -t upload`，需要从零校准时先显式擦除 NVS。
合并时**保留 bootloader 自带的 DIO 头**，不要强制 QIO。详见
[`build-and-flash.md`](build-and-flash.md)。

### 2.6 上机

Agent 必须**先问用户**，得到明确确认后才能烧录。烧录后：

1. 采集串口日志（先开串口再复位，避免漏掉启动输出）。
2. 按 [`testing.md`](testing.md) 的设备清单逐项验证。
3. 把无法验证的项写进 `Unverified`。

### 2.7 报告

```text
Build:        PASS / FAIL / NOT RUN
Host tests:   PASS / FAIL / NOT RUN
Device tests: PASS / FAIL / NOT RUN
Unverified:   ...
Artifacts:    ...（含 full.bin、SHA256、manifest.json、归档目录）
```

- 没跑就是 `NOT RUN`；编译通过只能写 `Build: PASS`。
- 不要用“应该可以”“理论上正常”代替实测。
- 改动射频/分区/启动配置时，单列风险段落。

---

## 3. 提示词模板

### 3.1 完整版（推荐）

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

### 3.2 最小版

```text
读 AGENTS.md 和 docs/README.md，基于本仓库的 BSP 实现：<需求>。
用 guachat_dev 环境，重新设计界面；先问清缺失的硬件信息；不要烧录。
交付前跑 pio run -e guachat_dev，并报告 Build / Unverified。
```

### 3.3 只做硬件自检

```text
烧录 guachat_dev 到板子，按 docs/testing.md 的设备清单逐项检查，
采集串口日志，报告实际读数与 Unverified 项。烧录前先问我。
```

---

## 4. Agent 容易犯的错

| 错误 | 正确做法 |
| --- | --- |
| 把 `dev_ui.cpp` 改个标题当新应用 | 重新设计布局、交互和视觉 |
| 在后台任务里直接 `lv_label_set_text` | 用队列/快照，在 UI 任务里更新 |
| 在 `uiTask` 里周期 `Serial.print` | 删除；需要日志时用低频或独立任务 |
| 直接 `Wire.beginTransmission` | 走 Manager / `i2cBusLock` |
| 删掉开机震动 | 本机没有电源指示灯；保留 `bsp_haptic_boot_feedback()` 或提供等效提示 |
| 新增 `src/apps/` 后忘了编译 | `main` 单环境，文件在 `src/` 下会被编译；跑一次 `pio run -e guachat_dev` 确认 |
| 合并固件强制 `--flash_mode qio` | 保留 DIO 头 |
| manifest 里的 SHA256 对得上就认为镜像正确 | 跑 `verify_firmware.py` 解析结构与 ELF 身份 |
| 中文直接 `lv_label_set_text("你好")` | 先确认字库覆盖，补子集字体 |
| 改 `src/bsp/bsp_pins.h` 不同步文档 | 两边一起改 |
| 编译通过就报告“功能正常” | 真机验证后再写 Device tests |

---

## 5. Skills

`skills/` 下的技能包把上面的流程拆成可按需加载的模块：

- `guachat-develop`：需求 → 实现 → 验证 → 报告
- `guachat-setup`：安装 PlatformIO / Python / 工具链
- `guachat-build`：构建、资源预算、合并/校验固件、发布归档
- `guachat-device-test`：确认后烧录、串口采集、设备清单
- `guachat-debug`：启动循环、花屏、I2C、SD、射频排障

这 5 个是开发前必需项：Agent 必须先确认当前环境能发现并读取它们，缺失时
自行安装；安装失败要报告阻塞点，不能凭目录存在宣称已加载。详见
[`AGENTS.md`](../AGENTS.md#11-必需技能开发前检查)。

Agent 不需要一次性读完全部；先看 `skills/*/SKILL.md` 的触发条件。

Claude Code 从项目内 `.claude/skills` 加载，安装一次即可：

```bash
python tools/install_skills.py             # 装到 .claude/skills
python tools/install_skills.py --print     # 只看源/目标路径
```

其他 Agent 若不支持这个目录，把 `skills/<name>/` 整个复制到它约定的位置，
或让用户直接读 `SKILL.md`。技能包索引见 [`../skills/README.md`](../skills/README.md)。
