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

1. 交付前跑完整门禁：
   ```bash
   python tools/validate.py --all
   ```
   它会执行静态检查、主机测试、构建、0x0 合并，并核对 `full.bin` 与
   `manifest.json` 中的大小、SHA256 和 ELF 身份。
2. 只检查构建时：
   ```bash
   pio run -e guachat_dev
   ```
   `main` 只有这一个环境。只构建你正在开发的应用。
3. 看体积报告：
   - `RAM` / `Flash` 行
   - `drom0_0_seg` 行
   - dev 基线很小，但字体和常量表会很快吃掉 DROM。
4. 检查产物：
   ```bash
   cat build/guachat-dev-full.bin.sha256
   cat build/guachat-dev-full.manifest.json
   ```
   manifest 用 SHA256 绑定 merged image、application、bootloader、
   partitions、ELF 和 MAP。
5. 报告：
   - 构建状态（`PASS / FAIL / NOT RUN`）
   - 内存百分比
   - 产物路径、大小、SHA256、manifest 路径
   - 警告（射频/无线提示不算失败）

## 手动合并

只复用现有构建重新合并时：

```bash
python tools/merge_firmware.py --no-build
```

不加 `--no-build` 会先构建再合并。

## 硬性规则

- 不要强制 `--flash_mode qio`。bootloader 头是 DIO，强制 QIO 会导致
  `TG0WDT_SYS_RST` 启动循环。
- 不要 `pio pkg update`，可能替换仓库内定制的 LVGL。
- 不要把外部 UI 生成器输出或应用业务重新引入 `main`。

## 产物

```text
build/guachat-dev-full.bin
build/guachat-dev-full.bin.sha256
build/guachat-dev-full.manifest.json
```

## 回退

- `docs/build-and-flash.md`
- `docs/troubleshooting.md`
