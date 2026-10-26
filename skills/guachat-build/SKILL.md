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

1. Build the development environment:
   ```bash
   pio run -e guachat_dev
   ```
   `main` has only this one environment; build only the application you are
   developing.
2. Read the size report:
   - `RAM` and `Flash` lines
   - `drom0_0_seg` line
   - the dev baseline is small, but fonts and constant tables grow DROM quickly.
3. Run validation:
   ```bash
   python tools/validate.py --static
   python tools/validate.py --host
   ```
4. Merge:
   ```bash
   python tools/merge_firmware.py
   ```
   This runs the build again unless `--no-build` is given.
5. Verify:
   ```bash
   cat build/guachat-dev-full.bin.sha256
   ```
6. Report:
   - Build status
   - memory percentages
   - artifact path, size, SHA256
   - warnings (RF/wireless notices are not failures)

## Hard rules

- Do not force `--flash_mode qio`. The bootloader header is DIO; QIO causes a
  `TG0WDT_SYS_RST` boot loop.
- Do not run `pio pkg update`; it may replace the vendored LVGL.
- Do not reintroduce UI-generator output or application business code into `main`.

## Outputs

```text
build/guachat-dev-full.bin
build/guachat-dev-full.bin.sha256
```

## Fallbacks

- `docs/build-and-flash.md`
- `docs/troubleshooting.md`
