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
   它会执行静态检查、主机测试、构建、0x0 合并，核对 `full.bin` 与
   `manifest.json` 中的大小、SHA256 和 ELF 身份，并运行
   `tools/verify_firmware.py` 独立解析镜像。
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
5. 独立校验（合并工具默认已执行，除非用了 `--no-verify`）：
   ```bash
   python tools/verify_firmware.py
   python tools/verify_firmware.py --image-only --image build/guachat-dev-full.bin
   ```
   检查芯片/Flash、DIO bootloader（QIO 直接失败）、分区表 magic 与 MD5、
   区间重叠/越界、app 分区、应用描述符、内嵌 ELF SHA256，以及
   `0x0/0x8000/0xE000/0x10000` 处的组件字节。manifest 的 SHA256 不能代替它。
6. 需要交付或存档时生成发布归档：
   ```bash
   python tools/archive_firmware.py create --env guachat_dev
   python tools/archive_firmware.py verify dist/firmware/<sha256>
   ```
   目录名是合并镜像 SHA256，内含镜像、校验和、manifest、`firmware.elf`、
   `firmware.map`、`README.txt` 与 `archive.json`。约 32 MB，`dist/` 不入库。
7. 报告：
   - 构建状态（`PASS / FAIL / NOT RUN`）
   - 内存百分比
   - 产物路径、大小、SHA256、manifest 路径、归档路径
   - 警告（射频/无线提示不算失败）

## 手动合并

只复用现有构建重新合并时：

```bash
python tools/merge_firmware.py --no-build
```

不加 `--no-build` 会先构建再合并。

## CI

- `.github/workflows/validate.yml` 运行 `validate.py --static --host`。
- `.github/workflows/firmware.yml` 构建 `guachat_dev`、合并、校验、归档，并上传
  镜像、校验和、manifest、恢复包与 `dist/firmware/**`（保留 14 天）。
- Gitee 不执行 GitHub Actions；从 Gitee 镜像交付前必须在本地跑同一套门禁。

## 硬性规则

- 从 `0x0` 刷 `full.bin` 会写入 `0x9000` 起的 NVS 区域和 `0xE000` 的 otadata，
  但**不保证清空** NVS：合并镜像在 NVS 区域是 0xFF 填充，2026-10-27 实测
  刷完后 DRV2605L 校准仍保留。需要保留 NVS 时改用
  `pio run -e guachat_dev -t upload`，或只把 `firmware.bin` 写到 `0x10000`；
  需要从零校准时先显式擦除 NVS。
- 不要强制 `--flash_mode qio`。bootloader 头是 DIO，强制 QIO 会导致
  `TG0WDT_SYS_RST` 启动循环。
- 不要 `pio pkg update`，可能替换仓库内定制的 LVGL。
- 不要把外部 UI 生成器输出或应用业务重新引入 `main`。

## 产物

```text
build/guachat-dev-full.bin
build/guachat-dev-full.bin.sha256
build/guachat-dev-full.manifest.json
dist/firmware/<sha256>/   # 仅归档时
```

## 回退

- `docs/build-and-flash.md`
- `docs/troubleshooting.md`
