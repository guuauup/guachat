# 文档索引

> 先读 [`AGENTS.md`](../AGENTS.md)。本页只负责“什么问题去哪篇文档”。
> 文档按主题拆分，不需要从头读到尾。

## 权威性

| 级别 | 含义 |
| --- | --- |
| **权威** | 与代码同步维护；冲突时优先 |
| **参考** | 解释背景、设计取舍或外部资料；可能滞后 |

硬件冲突的最终优先级：**实测 > `src/bsp/bsp_pins.h` > 驱动实现 > 文档 > README**。

---

## 入门与工作流

| 文档 | 级别 | 回答的问题 |
| --- | --- | --- |
| [`../AGENTS.md`](../AGENTS.md) | 权威 | Agent 必须遵守什么；边界、烧录、报告格式 |
| [`ai-guide.md`](ai-guide.md) | 权威 | 从需求到交付的完整工作流；提示词模板 |
| [`../CLAUDE.md`](../CLAUDE.md) | 权威 | Claude Code 的薄入口（指向 AGENTS.md） |
| [`../skills/README.md`](../skills/README.md) | 权威 | 技能包清单、安装方式、写法约定 |

## 工程

| 文档 | 级别 | 回答的问题 |
| --- | --- | --- |
| [`architecture.md`](architecture.md) | 权威 | 构建环境、启动顺序、任务模型、内存布局、数据流 |
| [`bsp-api.md`](bsp-api.md) | 权威 | 每个 BSP 模块的 API、线程约束、示例 |
| [`concurrency.md`](concurrency.md) | 权威 | LVGL 单任务模型、任务优先级、I2C/SD 互斥 |
| [`build-and-flash.md`](build-and-flash.md) | 权威 | 装环境、构建、合并/校验 0x0 固件、发布归档、CI、烧录、恢复 |
| [`testing.md`](testing.md) | 权威 | 静态检查、主机测试、镜像/归档校验、设备测试清单、报告格式 |
| [`fonts.md`](fonts.md) | 权威 | 中文字库、子集生成、覆盖检查 |
| [`release.md`](release.md) | 权威 | 版本、产物、发布归档、发布检查 |
| [`troubleshooting.md`](troubleshooting.md) | 参考 | 启动循环、花屏、I2C 卡死、射频异常 |

## 硬件

| 文档 | 级别 | 回答的问题 |
| --- | --- | --- |
| [`hardware/硬件连接说明.md`](hardware/硬件连接说明.md) | 权威 | 引脚、总线、上电顺序、外设连接 |
| [`hardware/电源树.md`](hardware/电源树.md) | 权威 | 电池、DCDC、电源域、开关机 |
| [`hardware/射频与合规.md`](hardware/射频与合规.md) | 权威 | 射频默认值、可修改范围、合规责任提示 |

## 应用边界

`main` 只包含 BSP、`src/dev/` 基线和依赖驱动；新应用从 `src/dev/` 开始，
界面用纯 LVGL 自建。

## 按任务找文档

| 任务 | 阅读顺序 |
| --- | --- |
| 第一次跑起来 | `build-and-flash.md` → `architecture.md` |
| 点灯/读传感器 | `bsp-api.md` → `src/bsp/bsp_sensors.h` |
| 做新界面 | `bsp-api.md`（display/input）→ `fonts.md` → `concurrency.md` |
| LoRa 收发 | `bsp-api.md`（radio）→ `hardware/射频与合规.md` |
| 加任务/定时器 | `concurrency.md` |
| 编译过了但开机崩 | `troubleshooting.md` → `testing.md` |
| 准备交付 | `testing.md` → `build-and-flash.md`（合并/校验/归档） → `release.md` |
