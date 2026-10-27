---
name: guachat-develop
description: Implement a new application or UI for the GUACHAT 2G4 board using the BSP. Use when the user asks for new functionality, a new screen, or a firmware feature.
---

# GUACHAT develop

## When to use

The user asks for a new app, screen, interaction, sensor feature, LoRa behavior,
or any firmware change on this board.

## Read first

1. `AGENTS.md` — mandatory contract.
2. `docs/README.md` — route to the right document.
3. `docs/bsp-api.md` — available hardware APIs.
4. `docs/concurrency.md` — LVGL and task rules.

## Workflow

1. **Clarify only what is missing.** Ask about unknown pins, sensor models,
   screen orientation, key polarity, or ambiguous requirements. Decide the rest.
2. **Protect the workspace.**
   ```bash
   git status
   git switch -c feature/<short-name>
   ```
   Never discard user changes.
3. **Design.**
   - Pick BSP modules; read their headers in `src/bsp/`.
   - Plan the UI from scratch; do not reuse the `dev_ui.cpp` layout.
   - For Chinese text, use `ui_font_syhtm18` from `src/fonts/fonts.h` first;
     generate a subset only if a glyph is missing (`docs/fonts.md`).
4. **Implement.**
   - Start from `src/main.cpp` for initialization order.
   - Keep the boot feedback in `hw_init_task`: `bsp_haptic_init()` plus
     `bsp_haptic_boot_feedback()`. This board has no power LED; one vibration
     tells the user the firmware booted. Keep `bsp_haptic_tick()` in `loop()`.
     Alert level 0 disables both alerts and boot feedback.
   - Call `bsp_haptic_key_click()` from button/slider/roller/switch events for
     low-voltage touch feedback. Use `bsp_haptic_get/set_alert_*()` and
     `bsp_haptic_get/set_key_feedback()` for persisted user settings.
   - Read the three programmable physical keys (BOOT/lock GPIO0, volume up P01,
     volume down P00) through `bsp_buttons.h`; the power key is hardware-only
     and cannot be read by the firmware.
   - `bsp_haptic_recalibrate()` blocks 1–3 s; call it from a background task,
     never from an LVGL callback.
   - Replace `src/dev/dev_ui.cpp` with your own screen.
   - Keep all `lv_*` calls in `ui_task` or LVGL callbacks.
   - Put background work in its own task; hand data to the UI via queue/snapshot.
   - Keep the UI self-contained: build it in code, not from generated UI files.
5. **Build and validate.**
   For quick iteration:
   ```bash
   pio run -e guachat_dev
   python tools/validate.py --static
   ```
   Before delivery, run the complete gate:
   ```bash
   python tools/validate.py --all
   ```
   It runs static checks, host tests, the build, the 0x0 merge, and verifies
   `full.bin` against the SHA256/ELF identity in `manifest.json`. The gate also
   runs `tools/verify_firmware.py`, which parses the image independently
   (chip/flash, DIO header, partition MD5, app descriptor, ELF identity).
   If the deliverable needs an archive:
   ```bash
   python tools/archive_firmware.py create --env guachat_dev
   python tools/archive_firmware.py verify dist/firmware/<sha256>
   ```
6. **Report** in `Build / Host tests / Device tests / Unverified / Artifacts`.
   Use `PASS / FAIL / NOT RUN`; a successful build is only `Build: PASS`.
7. **Ask before flashing.** Compilation or a connected USB cable is not consent.

## Hard rules

- Hardware constants live in `src/bsp/bsp_pins.h`; no naked GPIO numbers.
- Do not remove the one-shot boot vibration unless you provide an equivalent
  power-on indication. Use `bsp_haptic_*`, never DRV2605L registers directly.
- Do not call `bsp_haptic_recalibrate()` from the UI task; it blocks 1–3 s.
- Do not call `Wire`, `SPI`, `SD.begin`, `i2s_*`, or `radio.*` from app code.
- `main` contains only the BSP and dev baseline; build the interface with plain LVGL.
- LVGL has no global lock; only the UI task may call it.
- New tasks: Core 0 priority ≤ 6, Core 1 priority ≤ 5.
- Never add periodic `Serial` output to the UI task loop.
- Merge firmware without forcing `--flash_mode qio`.

## Fallbacks

- Build errors: `docs/troubleshooting.md`.
- Font issues: `docs/fonts.md`.
- Flash/merge: `docs/build-and-flash.md`.
- Device checks: `docs/testing.md`.
