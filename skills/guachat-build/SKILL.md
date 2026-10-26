---
name: guachat-build
description: Build GUACHAT firmware, check memory budgets, and produce a 0x0 merged image with checksum. Use when compiling, validating size, or preparing a flashable deliverable.
---

# GUACHAT build

## When to use

- The user asks to compile or produce firmware.
- You need to check RAM/Flash/DROM usage.
- You need a merged image flashable from `0x0`.

## Steps

1. Run the complete gate before delivery:
   ```bash
   python tools/validate.py --all
   ```
   It runs static checks, host tests, the build, the 0x0 merge, and verifies
   `full.bin` against `manifest.json` (size, SHA256, matching ELF).
2. For a build-only check:
   ```bash
   pio run -e guachat_dev
   ```
   `main` has only this one environment; build only the application you are
   developing.
3. Read the size report:
   - `RAM` and `Flash` lines
   - `drom0_0_seg` line
   - the dev baseline is small, but fonts and constant tables grow DROM quickly.
4. Inspect the artifacts:
   ```bash
   cat build/guachat-dev-full.bin.sha256
   cat build/guachat-dev-full.manifest.json
   ```
   The manifest binds the merged image, application, bootloader, partitions,
   ELF, and MAP with their SHA256.
5. Report:
   - Build status (`PASS / FAIL / NOT RUN`)
   - memory percentages
   - artifact path, size, SHA256, manifest path
   - warnings (RF/wireless notices are not failures)

## Manual merge

If you only need to re-merge an existing build:

```bash
python tools/merge_firmware.py --no-build
```

Without `--no-build`, the tool builds first and then merges.

## Hard rules

- Do not force `--flash_mode qio`. The bootloader header is DIO; QIO causes a
  `TG0WDT_SYS_RST` boot loop.
- Do not run `pio pkg update`; it may replace the vendored LVGL.
- Do not reintroduce UI-generator output or application business code into `main`.

## Outputs

```text
build/guachat-dev-full.bin
build/guachat-dev-full.bin.sha256
build/guachat-dev-full.manifest.json
```

## Fallbacks

- `docs/build-and-flash.md`
- `docs/troubleshooting.md`
