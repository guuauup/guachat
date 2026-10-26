# 第三方软件与资源声明

本项目包含下列第三方软件、字体和数据。各许可证全文位于
[`third_party/licenses/`](third_party/licenses/)；组件自带 LICENSE 文件的，以该文件为准。

本文件不是法律意见。发布二进制固件时，请同时阅读第 3 节。

`main` 只编译 BSP 与 `src/dev/` 的最小基线。仓库自带的 `ui_font_syhtm18`
是独立生成的 Noto Sans SC 位图子集。

---

## 1. 第三方代码

### 1.1 LVGL 及内置组件

| 组件 | 许可证 | 位置 / 版权 |
|---|---|---|
| LVGL 8.3.11 | MIT | [`lib/lvgl/`](lib/lvgl/)，Copyright (c) 2021 LVGL Kft；全文见 `LVGL-LICENCE.txt` |
| LodePNG | zlib | `lib/lvgl/src/extra/libs/png/`，Copyright (c) 2005-2020 Lode Vandevenne；全文见 `Zlib-LodePNG.txt` |
| TJpgDec R0.03 | ChaN 宽松条款 | `lib/lvgl/src/extra/libs/sjpg/`，Copyright (C) 2021 ChaN；全文见 `ChaN-TJpgDec.txt` |
| QR Code generator (C) | MIT | `lib/lvgl/src/extra/libs/qrcode/`，Copyright (c) Project Nayuki |
| gifdec | Public Domain | `lib/lvgl/src/extra/libs/gif/`，上游 <https://github.com/lecram/gifdec>；`LV_USE_GIF=0`，未编译 |
| stb_rect_pack / stb_truetype_htcw | Public Domain | `lib/lvgl/src/extra/libs/tiny_ttf/`，Sean Barrett / htcw fork |

### 1.2 显示、触摸与 GPIO

| 组件 | 许可证 | 版权 |
|---|---|---|
| TFT_eSPI | MIT + Adafruit_GFX BSD + GFXFF 字体许可 | Bodmer 及贡献者；完整许可见 `TFT_eSPI.txt` |
| FT6336U | MIT | Copyright (c) Atsushi Sasaki；`lib/Touch_Drivers/LICENSE` |
| TCA9555 | MIT | Copyright (c) 2021-2026 Rob Tillaart |
| I2C_Functions | MIT | Copyright (c) 2022 Mehmet Günce Akkoyun |
| MAX17055 | MIT | Copyright (c) 2022 Mehmet Günce Akkoyun |

### 1.3 无线与传感器

| 组件 | 许可证 | 版权 |
|---|---|---|
| RadioLib 7.7.1 | MIT | Copyright (c) 2018 Jan Gromeš；全文见 `RadioLib-MIT.txt` |
| Adafruit BusIO | MIT | Copyright (c) 2017 Adafruit Industries |
| Adafruit QMC5883P | MIT | Adafruit Industries（头文件注明 MIT） |
| Adafruit LSM6DS | BSD 3-Clause | Copyright (c) 2019 Bryan Siepert for Adafruit Industries |
| Adafruit BMP5xx | BSD 3-Clause | Copyright (c) 2024 Adafruit Industries |
| Adafruit Unified Sensor | Apache-2.0 | Adafruit Industries；全文见 `Apache-2.0.txt` |
| Sensirion SHT (arduino-sht) | BSD 3-Clause | Copyright (c) 2018, Sensirion AG |

### 1.4 LVGL 内置字体

| 字体 | 状态 | 许可证 / 版权 |
|---|---|---|
| Montserrat 10/12/14/16/18/20/22/24/30/48（14 为默认字体） | 启用 | SIL OFL-1.1，Copyright (c) 2011 The Montserrat Project Authors |
| Montserrat 文件内嵌的 Font Awesome 5 图标字形 | 启用 | 字体 SIL OFL-1.1；图标 CC BY 4.0；代码 MIT，Copyright (c) Fonticons, Inc. |
| DejaVu 16 Persian/Hebrew | 未启用（仅源码） | Bitstream Vera License，Copyright (c) 2003 Bitstream, Inc.；全文见 `Bitstream-Vera.txt` |
| SimSun 16 CJK | 未启用（`LV_FONT_SIMSUN_16_CJK=0`） | LVGL 上游自动生成的转换文件，源字体为 SimSun |
| UNSCII 8/16 | 未启用 | Public Domain 风格 |
| `ui_font_syhtm18`（18 px / bpp4） | 启用，位于 [`src/fonts/`](src/fonts/) | 由 Noto Sans SC Medium 转换的位图子集；SIL OFL-1.1，Copyright 2014-2021 Adobe，Reserved Font Name 'Source'；全文与版权声明见 `NotoSansSC-OFL.txt` |

OFL-1.1 全文见 `OFL-1.1.txt`。Font Awesome 免费版说明：
<https://fontawesome.com/license/free>。

`ui_font_syhtm18` 是格式转换后的派生作品，不是原始字体文件；按 OFL-1.1 第 5 条，
该字体部分仍以 OFL-1.1 分发。它没有使用 Reserved Font Name 'Source' 作为字体名。

---

## 2. 素材与数据

仓库不包含第三方 UI 图标、Logo 或数据文件；`src/dev/` 的界面全部由纯 LVGL 绘制。

---

## 3. 发布二进制固件时的附加义务

只发布源码时，本节可暂缓；发布 `.bin` / 量产固件时需满足：

0. **GUACHAT 应用代码 — GPL-3.0-or-later**
   - 发布 `.bin` / 量产固件时，须同时向接收者提供完整对应源码（即本仓库）或 GPL-3.0
     第 6 条允许的书面要约；
   - 基于本项目修改的固件，同样须以 GPL-3.0（或更高版本）提供完整对应源码。

1. **Arduino-ESP32 核心 — LGPL-2.1**
   - 发布二进制时，需提供核心库源码或可重新链接的目标文件；
   - 本项目只发布源码时，此义务自然满足。

2. **ESP-IDF — Apache-2.0**
   - 随附 Apache-2.0 许可证，并保留其 NOTICE（如有）。

3. **xtensa 工具链 — GPL + runtime exception**
   - 编译产物不受 GPL 约束；只有分发工具链本身时才需要提供工具链源码。

---

## 4. 许可证全文索引

| 文件 | 适用组件 |
|---|---|
| `third_party/licenses/MIT.txt` | LVGL、RadioLib、TFT_eSPI、TCA9555、Adafruit BusIO/QMC5883P、qrcodegen、I2C_Functions、MAX17055、FT6336U |
| `third_party/licenses/BSD-3-Clause.txt` | Adafruit LSM6DS、Adafruit BMP5xx、Sensirion SHT |
| `third_party/licenses/Apache-2.0.txt` | Adafruit Unified Sensor |
| `third_party/licenses/LGPL-2.1.txt` | Arduino-ESP32 核心 |
| `third_party/licenses/OFL-1.1.txt` | Montserrat、Font Awesome 字体部分 |
| `third_party/licenses/NotoSansSC-OFL.txt` | `ui_font_syhtm18`（Noto Sans SC / 思源黑体派生位图字体） |
| `third_party/licenses/Bitstream-Vera.txt` | LVGL 内置 DejaVu 16（未启用，仅源码） |
| `third_party/licenses/Zlib-LodePNG.txt` | LodePNG |
| `third_party/licenses/ChaN-TJpgDec.txt` | TJpgDec |
| `third_party/licenses/TFT_eSPI.txt` | TFT_eSPI 及其捆绑字体 |
| `third_party/licenses/LVGL-LICENCE.txt` | LVGL |

各依赖的原始许可证文件另保留一份：

`RadioLib-MIT.txt`、`TCA9555-MIT.txt`、`Adafruit-BusIO-MIT.txt`、
`Adafruit-LSM6DS-BSD.txt`、`Adafruit-BMP5xx-BSD.txt`、`Sensirion-SHT-BSD.txt`。
