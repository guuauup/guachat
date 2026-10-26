# CLAUDE.md

本仓库的完整 Agent 契约在 **[AGENTS.md](AGENTS.md)**，请先完整阅读。

必须记住的几条：

- 默认环境是 `guachat_dev`，用户应用从 `src/dev/` 开始；`main` 只含 BSP 与开发基线。
- 硬件参数唯一来源是 `src/bsp/bsp_pins.h`；不要猜引脚。
- 所有 `lv_*` 调用只能在 LVGL 任务（`ui_task`）里；本项目没有全局 LVGL 锁。
- 未经用户确认不得烧录；合并固件时不要强制 `--flash_mode qio`。
- 不要引入外部 UI 生成器输出或应用业务代码。
- 交付报告分开写 `Build / Host tests / Device tests / Unverified`。
