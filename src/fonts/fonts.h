/**
 * fonts.h — 项目内可用的 LVGL 字体
 *
 * ui_font_syhtm18: 18 px / bpp4，基于 Noto Sans SC Medium（思源黑体同源，
 * SIL OFL-1.1）生成的 GB2312 级中文字符集，含 ASCII。
 * 覆盖约 6900 个常用汉字及常用标点/符号，位图放在 Flash/DROM，不占内存池。
 *
 * 用法：
 *   #include "fonts/fonts.h"
 *   lv_obj_set_style_text_font(label, &ui_font_syhtm18, 0);
 *
 * 许可证与再分发要求见 third_party/licenses/NotoSansSC-OFL.txt 和
 * THIRD_PARTY_NOTICES.md。
 */
#pragma once

#include <lvgl.h>

LV_FONT_DECLARE(ui_font_syhtm18);
