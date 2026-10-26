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
   `full.bin` against the SHA256/ELF identity in `manifest.json`.
6. **Report** in `Build / Host tests / Device tests / Unverified / Artifacts`.
   Use `PASS / FAIL / NOT RUN`; a successful build is only `Build: PASS`.
7. **Ask before flashing.** Compilation or a connected USB cable is not consent.

## Hard rules

- Hardware constants live in `src/bsp/bsp_pins.h`; no naked GPIO numbers.
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
