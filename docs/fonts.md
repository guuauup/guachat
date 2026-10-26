# 中文字体与字库覆盖

> `guachat_dev` 的自检界面是中文，使用仓库自带的 18 px 中文字体 `ui_font_syhtm18`。
> 其他控件若没有显式设置字体，仍会回落到 LVGL 内置的 Montserrat 14，中文会显示成
> 方块或空白。显示中文时给控件（或其父容器，字体可继承）设置该字体即可。

---

## 1. 当前配置

| 项目 | 值 |
| --- | --- |
| LVGL 版本 | 8.3（仓库内定制版 `lib/lvgl`） |
| 默认字体 | `lv_font_montserrat_14`（`lv_conf.h` 中 `LV_FONT_DEFAULT`；`dev_ui` 已覆盖为 `ui_font_syhtm18`） |
| 已启用的内置字体 | Montserrat 10 / 12 / 14 / 16 / 18 / 20 / 22 / 24 / 30 / 48 |
| 未启用的字号 | 8 / 26 / 28 / 32–46；需要时先在 `lv_conf.h` 打开 |
| 项目内置中文字体 | `ui_font_syhtm18`（18 px / bpp4，`src/fonts/`） |
| 中文字形覆盖 | 约 6900 个常用汉字（GB2312 级）+ ASCII + 常用标点/符号 |
| 中文输入法 | 关闭（`LV_USE_IME_PINYIN 0`）；需要时自行实现 |
| 内存池 | PSRAM 512 KB（字体位图放 Flash/DROM，不占池） |

`ui_font_syhtm18` 由 Noto Sans SC Medium 转换而来，采用 SIL OFL-1.1，可以随
固件嵌入和再分发；版权与许可证见 `third_party/licenses/NotoSansSC-OFL.txt`。
它只覆盖常用汉字，不是完整 Unicode 字库；生僻字或界面专用字符仍需按本文生成
子集字体。

---

## 2. 直接使用内置中文字体

```cpp
#include "fonts/fonts.h"

lv_obj_t *label = lv_label_create(lv_scr_act());
lv_obj_set_style_text_font(label, &ui_font_syhtm18, 0);
lv_label_set_text(label, "温度 25.3 °C");
```

如果整个屏幕都用中文，可以给屏幕容器设置一次，子控件继承：

```cpp
lv_obj_set_style_text_font(lv_scr_act(), &ui_font_syhtm18, 0);
```

要点：

- 字体是只读常量，位图在 Flash/DROM，不需要拷到 PSRAM。
- 同一个控件不要混用 Montserrat 和中文字体；父容器设置后子控件会继承。
- 默认字体没有改动；没有代码引用该字体时，链接器会把整套字体丢掉，
  基线固件大小不变（实测 672 KB）。
- 一旦被引用，固件约增加 1.1 MB，Flash 仍只占 16 MB 的约 11.7%。
- 该字体是 18 px 单一字号；需要其他字号时重新生成。
- 所有 `lv_*` 调用仍只能在 `ui_task` 或 LVGL 回调里执行。

---

## 3. 何时需要生成字体

出现以下任一情况：

- 界面里出现中文、日文、韩文或特殊符号
- 需要更大的字号/更粗的字重
- 需要图标字形（LVGL symbol 字体之外的私有图标）

---

## 4. 生成子集字体

### 4.1 安装工具

```bash
npm install -g lv_font_conv
# 或使用 npx，无需全局安装
```

需要一份支持中文的 TTF，例如 Noto Sans SC / 思源黑体（OFL-1.1）。
**注意字体许可证**：随固件分发前确认允许嵌入/再分发，并更新
`THIRD_PARTY_NOTICES.md`。阿里巴巴普惠体的官方许可不允许转换字体格式，
不要用它生成 LVGL 位图字体。

### 4.2 只收集实际用到的字符（推荐）

全量 CJK 在 16 px / bpp4 下会达到数 MB，直接吃掉 DROM。先收集界面里出现的字符：

```bash
python tools/check_fonts.py --collect
# 指定目录：python tools/check_fonts.py --collect --src src/dev
```

工具会输出用到的字符列表。也可以手工收集：

```bash
python - <<'PY'
import re, pathlib
chars = set()
for p in pathlib.Path('src/dev').rglob('*.cpp'):
    chars |= set(re.findall(r'[一-鿿　-〿＀-￯]', p.read_text('utf-8')))
print(''.join(sorted(chars)))
PY
```

### 4.3 生成

```bash
lv_font_conv \
  --font NotoSansSC-Regular.ttf \
  --size 16 \
  --bpp 4 \
  --format lvgl \
  --no-compress \
  --symbols "设置返回确定取消温度湿度气压电量连接中未连接" \
  --range 0x20-0x7F \
  -o src/fonts/font_zh16.c
```

内置 `ui_font_syhtm18` 的生成参数是 `--size 18 --bpp 4 --no-compress`，
字体为 `NotoSansSC-Medium.ttf`，字符集为 GB2312 级常用汉字加
`--range 32-127`；完整参数记录在 `src/fonts/ui_font_syhtm18.c` 文件头。

参数建议：

| 参数 | 建议 |
| --- | --- |
| `--size` | 12 / 14 / 16 / 20；先用小字号 |
| `--bpp` | 4（体积与显示效果平衡）；小字号可用 2 |
| `--no-compress` | 开发阶段便于排查；量产可去掉以省空间 |
| `--range 0x20-0x7F` | 保留 ASCII |
| `--symbols` | 只放界面实际用到的字符 |

### 4.4 在代码里使用

```cpp
LV_FONT_DECLARE(font_zh16);

lv_obj_t *label = lv_label_create(lv_scr_act());
lv_obj_set_style_text_font(label, &font_zh16, 0);
lv_label_set_text(label, "温度 25.3 °C");
```

如果整个屏幕都用中文，可以给屏幕容器设置一次，子控件继承：

```cpp
lv_obj_set_style_text_font(lv_scr_act(), &font_zh16, 0);
```

---

## 5. 覆盖检查

### 5.1 自动检查

```bash
python tools/check_fonts.py --src src/dev --font src/fonts/font_zh16.c
python tools/validate.py --static     # 也会检查 src/dev 的中文字体覆盖
```

期望：所有中文码点都在字体文件的 `unicode_list` / range 内。
缺字时脚本返回非 0，并打印缺失字符。注释里的中文不会计入。

### 5.2 手工检查

1. 在界面里逐页浏览，确认没有 `□` 或空白。
2. 用串口观察 LVGL 日志（若开启），缺字通常会报 `No glyph`。
3. 用不同字号检查小字是否糊、粗体是否缺字重。

---

## 6. 体积与性能

- 字体位图进入 **DROM**（8 MB 硬限），字体与常量表是主要增长点。
- `ui_font_syhtm18` 在代码中被引用后，固件约增加 **1.15 MB**（位图约 1.0 MB、
  字形描述约 110 KB、Unicode 映射表约数十 KB）。
- `guachat_dev` 自检界面引用该字体，当前固件约 **1.82 MB**。
- 如果应用完全不引用中文字体，链接器会通过 `--gc-sections` 把整套字体丢掉，
  固件回到约 **672 KB**。
- 16 px / bpp4 / 1000 个汉字约数百 KB；全量 2 万汉字可达数 MB。
- 大字号（40 px 以上）优先只保留少量字符。
- 不要同时启用多套大字号中文字体。
- 字体是只读常量，放 Flash 即可，不要复制到 PSRAM。

---

## 7. 常见问题

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 中文显示方块 | 字体缺码点 | 把字符加入 `--symbols` 重新生成 |
| 中文完全不显示 | 没设置 `text_font`，仍用 Montserrat | 给控件或父容器设置中文字体 |
| 编译报 DROM 溢出 | 字体太大 | 减少字符/字号/bpp，或拆分多套小字体 |
| 英文数字也变方块 | 子集没包含 ASCII | 加 `--range 0x20-0x7F` |
| 字重不对 | 只生成了一个字重 | 粗体/细体分别生成并设置 |
| 字体文件找不到 | 未加入构建源 | `main` 只有 `guachat_dev` 一个环境，文件放在 `src/` 下即可被编译 |
