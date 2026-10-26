---
name: guachat-build
description: 构建 GUACHAT 固件、检查内存预算、生成带校验和的 0x0 合并固件。编译、检查体积或准备可刷写交付物时使用。
---

# GUACHAT 构建

## 触发条件

- 用户要求编译或产出固件。
- 需要检查 RAM/Flash/DROM 占用。
- 需要从 `0x0` 可刷写的合并镜像。

## 步骤

1. 构建开发环境：
   ```bash
   pio run -e guachat_dev
   ```
   `main` 只有这一个环境。只构建你正在开发的应用。
2. 看体积报告：
   - `RAM` / `Flash` 行
   - `drom0_0_seg` 行
   - dev 基线很小，但字体和常量表会很快吃掉 DROM。
3. 验证：
   ```bash
   python tools/validate.py --static
   python tools/validate.py --host
   ```
4. 合并：
   ```bash
   python tools/merge_firmware.py
   ```
   加 `--no-build` 可复用现有构建。
5. 校验：
   ```bash
   cat build/guachat-dev-full.bin.sha256
   ```
6. 报告：
   - 构建状态
   - 内存百分比
   - 产物路径、大小、SHA256
   - 警告（射频/无线提示不算失败）

## 硬性规则

- 不要强制 `--flash_mode qio`。bootloader 头是 DIO，强制 QIO 会导致
  `TG0WDT_SYS_RST` 启动循环。
- 不要 `pio pkg update`，可能替换仓库内定制的 LVGL。
- 不要把外部 UI 生成器输出或应用业务重新引入 `main`。

## 产物

```text
build/guachat-dev-full.bin
build/guachat-dev-full.bin.sha256
```

## 回退

- `docs/build-and-flash.md`
- `docs/troubleshooting.md`
