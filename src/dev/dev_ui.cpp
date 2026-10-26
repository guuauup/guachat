/**
 * dev_ui.cpp — 硬件自检页
 *
 * 所有 LVGL 调用都发生在 uiTask（lv_timer_handler / 事件回调）里。
 * 这里每 1 秒读取一次硬件；阻塞式 LoRa rx 不在 UI 里使用。
 *
 * 界面为中文，使用仓库自带的 ui_font_syhtm18（见 src/fonts/fonts.h）。
 */
#include "dev_ui.h"

#include <Arduino.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "bsp/bsp_audio.h"
#include "bsp/bsp_display.h"
#include "bsp/bsp_gps.h"
#include "bsp/bsp_pins.h"
#include "bsp/bsp_power.h"
#include "bsp/bsp_radio.h"
#include "bsp/bsp_sensors.h"
#include "bsp/bsp_storage.h"
#include "fonts/fonts.h"

namespace {

lv_obj_t *s_power_lbl  = nullptr;
lv_obj_t *s_sensor_lbl = nullptr;
lv_obj_t *s_radio_lbl  = nullptr;
lv_obj_t *s_tx_lbl     = nullptr;
lv_obj_t *s_gps_lbl    = nullptr;
lv_obj_t *s_audio_lbl  = nullptr;
lv_obj_t *s_bl_label   = nullptr;

/* 背光：滑块直接对应 LEDC 占空比 71..170，标签显示 1..100%（duty - 70）。
   测试固件不做持久化，开机回默认值。 */
constexpr int kBacklightMin     = 71;
constexpr int kBacklightMax     = BSP_LCD_BL_DUTY_SAFE_MAX;
constexpr int kBacklightDefault = 100;

/* LoRa 自检任务：勾选按钮后按固定周期发包，未勾选时只收包。
   LVGL 控件只由 ui_task 更新，这个任务只改下面的快照。 */
constexpr uint32_t kTxIntervalMs = 500;

volatile bool  s_tx_continuous = false;
SemaphoreHandle_t s_state_lock = nullptr;
uint32_t s_rx_count = 0;
uint32_t s_tx_count = 0;
bool     s_last_tx_ok = true;
char     s_last_packet[48] = "(none)";

struct RadioSnapshot {
    uint32_t rx_count;
    uint32_t tx_count;
    bool     last_tx_ok;
    char     last_packet[48];
};

void radio_record_rx(const uint8_t *packet, size_t len)
{
    if (!s_state_lock) {
        return;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    ++s_rx_count;
    const size_t n = (len < sizeof(s_last_packet) - 1)
                         ? len : sizeof(s_last_packet) - 1;
    for (size_t i = 0; i < n; ++i) {
        s_last_packet[i] = isprint((unsigned char)packet[i])
                               ? (char)packet[i] : '.';
    }
    s_last_packet[n] = '\0';
    xSemaphoreGive(s_state_lock);
}

void radio_record_tx(bool ok)
{
    if (!s_state_lock) {
        return;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    ++s_tx_count;
    s_last_tx_ok = ok;
    xSemaphoreGive(s_state_lock);
}

void radio_snapshot(RadioSnapshot &out)
{
    if (s_state_lock) {
        xSemaphoreTake(s_state_lock, portMAX_DELAY);
    }
    out.rx_count = s_rx_count;
    out.tx_count = s_tx_count;
    out.last_tx_ok = s_last_tx_ok;
    memcpy(out.last_packet, s_last_packet, sizeof(out.last_packet));
    if (s_state_lock) {
        xSemaphoreGive(s_state_lock);
    }
}

lv_obj_t *add_info_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_line_space(label, 2, 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    return label;
}

lv_obj_t *add_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb,
                     lv_event_code_t event = LV_EVENT_CLICKED)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 148, 44);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 0, -4);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x319795), 0);
    lv_obj_add_event_cb(btn, cb, event, nullptr);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

/* 内置中文字体是 18 px；递归覆盖整棵控件树，避免局部仍用 Montserrat 14 */
void apply_chinese_font(lv_obj_t *obj)
{
    lv_obj_set_style_text_font(obj, &ui_font_syhtm18, 0);
    const uint32_t count = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < count; ++i) {
        apply_chinese_font(lv_obj_get_child(obj, i));
    }
}

/* ---------------- 事件 ---------------- */

void beep_cb(lv_event_t *e)
{
    (void)e;
    bsp_audio_beep(1000, 100);
}

void tx_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    s_tx_continuous = lv_obj_has_state(btn, LV_STATE_CHECKED);
    if (s_tx_lbl) {
        lv_label_set_text(s_tx_lbl, s_tx_continuous ? "TX: 持续发送"
                                                    : "TX: 仅接收");
    }
}

/* 独立的 LoRa 收发任务：不在 ui_task 里做阻塞发送。
   勾选时每 500 ms 发一包 PING，其余时间保持接收；未勾选时只接收。 */
void radio_test_task(void *)
{
    uint32_t last_tx_ms = 0;

    for (;;) {
        bsp_radio_info_t info = {};
        bsp_radio_info(&info);
        if (!info.ready) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (bsp_radio_available()) {
            uint8_t packet[64];
            size_t len = 0;
            if (bsp_radio_read(packet, sizeof(packet), &len) == 0) {
                radio_record_rx(packet, len);
            }
            bsp_radio_start_rx();
        }

        if (s_tx_continuous) {
            const uint32_t now = millis();
            if (last_tx_ms == 0 ||
                (uint32_t)(now - last_tx_ms) >= kTxIntervalMs) {
                const bool ok = bsp_radio_send((const uint8_t *)"PING", 4);
                radio_record_tx(ok);
                bsp_radio_start_rx();
                last_tx_ms = now ? now : 1;
            }
        } else {
            last_tx_ms = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void volume_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    bsp_audio_set_volume((uint8_t)lv_slider_get_value(slider));
}

void backlight_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    const int duty = (int)lv_slider_get_value(slider);
    bsp_backlight_set_duty((uint16_t)duty);
    if (s_bl_label) {
        lv_label_set_text_fmt(s_bl_label, "%d%%", duty - (kBacklightMin - 1));
    }
}

/* ---------------- 1 Hz 刷新 ---------------- */

void update_cb(lv_timer_t *timer)
{
    (void)timer;
    char buf[640];

    /* 电源 + TF 卡 */
    bsp_power_info_t p = {};
    const bool power_ok = bsp_power_read(&p);
    bsp_storage_info_t sd = {};
    bsp_storage_info(&sd);

    snprintf(buf, sizeof(buf),
             "电池 (MAX17055)\n"
             "  状态 : %s\n"
             "  电压 : %.2f V\n"
             "  电流 : %+d mA%s\n"
             "  电量 : %u %%\n"
             "  剩余 : %u / %u mAh\n\n"
             "TF 卡 (SDMMC 单线)\n"
             "  %s\n"
             "  可用 : %.2f GB\n"
             "  总计 : %.2f GB\n"
             "  类型 : %s @ %.1f MHz",
             power_ok ? "正常" : "未检测到",
             power_ok ? p.voltage_mv / 1000.0f : 0.0f,
             power_ok ? p.current_ma : 0,
             (power_ok && p.charging) ? " (充电中)" : "",
             power_ok ? (unsigned)p.soc_percent : 0u,
             power_ok ? p.remaining_mah : 0u,
             power_ok ? p.design_mah : 0u,
             sd.mounted ? "已挂载到 /sd" : "未挂载",
             (double)sd.free_bytes / 1073741824.0,
             (double)sd.total_bytes / 1073741824.0,
             sd.mounted ? sd.card_type : "-",
             (double)sd.bus_freq_hz / 1000000.0);
    if (s_power_lbl) {
        lv_label_set_text(s_power_lbl, buf);
    }

    /* 传感器 */
    bsp_sensors_info_t s = {};
    bsp_sensors_read(&s);

    char rtc[32];
    if (s.rtc_ok) {
        snprintf(rtc, sizeof(rtc), "%04u-%02u-%02u %02u:%02u:%02u",
                 s.rtc_year, s.rtc_month, s.rtc_day,
                 s.rtc_hour, s.rtc_minute, s.rtc_second);
    } else {
        snprintf(rtc, sizeof(rtc), "未检测到");
    }

    snprintf(buf, sizeof(buf),
             "环境\n"
             "  SHT30 : %s  %.1f C  %.1f %%RH\n"
             "  露点  : %.1f C\n"
             "  BMP581: %s  %.1f hPa\n\n"
             "运动\n"
             "  IMU   : %s\n"
             "  加速度: %.1f  %.1f  %.1f m/s2\n"
             "  角速度: %.2f  %.2f  %.2f rad/s\n"
             "  磁力  : %s  %.1f  %.1f  %.1f\n"
             "  航向  : %.0f deg\n"
             "  俯仰/横滚: %.1f / %.1f deg\n\n"
             "RTC PCF8563T\n"
             "  %s\n"
             "  VL 标志: %s",
             s.env_ok ? "正常" : "n/a",
             s.temperature_c, s.humidity_pct, s.dew_point_c,
             s.baro_ok ? "正常" : "n/a", s.pressure_pa / 100.0f,
             s.imu_ok ? "正常" : "n/a",
             s.accel_mps2[0], s.accel_mps2[1], s.accel_mps2[2],
             s.gyro_rads[0], s.gyro_rads[1], s.gyro_rads[2],
             s.mag_ok ? "正常" : "n/a", s.mag[0], s.mag[1], s.mag[2],
             s.heading_deg, s.pitch_deg, s.roll_deg,
             rtc,
             s.rtc_voltage_low ? "低" : "正常");
    if (s_sensor_lbl) {
        lv_label_set_text(s_sensor_lbl, buf);
    }

    /* LoRa：收发由 radio_test_task 负责，这里只读取快照。 */
    RadioSnapshot radio_state = {};
    radio_snapshot(radio_state);

    bsp_radio_info_t r = {};
    bsp_radio_info(&r);
    const char *tx_state = (radio_state.tx_count == 0)
                               ? "尚未发送"
                               : (radio_state.last_tx_ok ? "最近成功"
                                                         : "最近失败");
    snprintf(buf, sizeof(buf),
             "LoRa SX1280 (E28-2G4M20S)\n"
             "  状态 : %s\n"
             "  频率 : %.1f MHz\n"
             "  扩频 : SF%u\n"
             "  带宽 : %.1f kHz\n"
             "  功率 : %d dBm (模块)\n"
             "  收包 : %lu\n"
             "  发包 : %lu (%s)\n\n"
             "最近数据包:\n"
             "  %s\n\n"
             "%s",
             r.ready ? "正常" : "未检测到",
             (double)r.freq_mhz, r.sf, (double)r.bw_khz, r.power_dbm,
             (unsigned long)radio_state.rx_count,
             (unsigned long)radio_state.tx_count, tx_state,
             radio_state.last_packet,
             s_tx_continuous
                 ? "持续发送中：500 ms/包；取消勾选后只接收。"
                 : "仅接收；点按“持续发送”开始 500 ms 周期发送。");
    if (s_radio_lbl) {
        lv_label_set_text(s_radio_lbl, buf);
    }
    if (s_tx_lbl) {
        lv_label_set_text(s_tx_lbl, s_tx_continuous ? "TX: 持续发送"
                                                    : "TX: 仅接收");
    }

    /* GPS */
    const gps_data_t *g = bsp_gps_get();
    if (!g || g->lastPvtMs == 0) {
        snprintf(buf, sizeof(buf),
                 "GPS MAX-M10S (UART0)\n"
                 "  模块 : %s\n"
                 "  等待 NAV-PVT...\n\n"
                 "室内通常无法定位;\n"
                 "请把设备靠近窗户。",
                 gpsModuleStatus == 0 ? "已配置" : "配置中");
    } else {
        snprintf(buf, sizeof(buf),
                 "GPS MAX-M10S (UART0)\n"
                 "  定位 : %s  (%u 已用 / %u 可见)\n"
                 "  纬度 : %.7f\n"
                 "  经度 : %.7f\n"
                 "  海拔 : %.1f m\n"
                 "  速度 : %.2f m/s\n"
                 "  UTC  : %04u-%02u-%02u %02u:%02u:%02u\n"
                 "  精度 : %.1f m\n"
                 "  更新 : %lu ms 前",
                 g->fixType >= 3 ? "3D" : (g->fixType == 2 ? "2D" : "未定位"),
                 g->numSV, g->numSvs,
                 g->lat, g->lon,
                 g->hMSL / 1000.0f,
                 g->gSpeed / 1000.0f,
                 g->year, g->month, g->day, g->hour, g->min, g->sec,
                 g->hAcc / 1000.0f,
                 (unsigned long)(millis() - g->lastPvtMs));
    }
    if (s_gps_lbl) {
        lv_label_set_text(s_gps_lbl, buf);
    }

    /* 音频：读一小段麦克风，显示峰值 */
    int16_t pcm[128];
    const size_t got = bsp_audio_read_pcm16(pcm, 128, 20);
    int peak = 0;
    for (size_t i = 0; i < got; ++i) {
        const int v = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (v > peak) {
            peak = v;
        }
    }
    snprintf(buf, sizeof(buf),
             "I2S 音频\n"
             "  扬声器: MAX98357\n"
             "  麦克风: ZTS6672\n"
             "  格式  : 默认 8 kHz / 32-bit / 立体声\n\n"
             "  麦克风峰值: %d\n\n"
             "音量滑块和蜂鸣按钮\n"
             "用于测试扬声器通路。",
             peak);
    if (s_audio_lbl) {
        lv_label_set_text(s_audio_lbl, buf);
    }
}

}  // namespace

void dev_ui_build(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    lv_obj_t *tv = lv_tabview_create(scr, LV_DIR_TOP, 42);
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x1A202C), 0);

    /* LVGL 8.3: 标签栏是一个 btnmatrix，用 LV_PART_ITEMS 上色 */
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tv);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x1A202C), 0);
    lv_obj_set_style_text_color(tab_btns, lv_color_hex(0xA0AEC0),
                                LV_PART_ITEMS);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x319795),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(tab_btns, lv_color_hex(0xFFFFFF),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
    /* 中文标签 18 px，收紧 item 内边距，避免 5 个页签被裁字 */
    lv_obj_set_style_pad_all(tab_btns, 2, LV_PART_ITEMS);

    lv_obj_t *tab_power  = lv_tabview_add_tab(tv, "电源");
    lv_obj_t *tab_sensor = lv_tabview_add_tab(tv, "传感器");
    lv_obj_t *tab_radio  = lv_tabview_add_tab(tv, "LoRa");
    lv_obj_t *tab_gps    = lv_tabview_add_tab(tv, "GPS");
    lv_obj_t *tab_audio  = lv_tabview_add_tab(tv, "音频");

    lv_obj_t *tabs[] = {tab_power, tab_sensor, tab_radio, tab_gps, tab_audio};
    for (lv_obj_t *tab : tabs) {
        lv_obj_set_style_bg_color(tab, lv_color_hex(0x101418), 0);
        lv_obj_set_style_pad_all(tab, 8, 0);
        lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_AUTO);
    }

    s_power_lbl  = add_info_label(tab_power);
    s_sensor_lbl = add_info_label(tab_sensor);
    s_radio_lbl  = add_info_label(tab_radio);
    s_gps_lbl    = add_info_label(tab_gps);
    s_audio_lbl  = add_info_label(tab_audio);

    /* 背光亮度：滑块 71..170 → 1..100%；测试固件不持久化 */
    constexpr int duty = kBacklightDefault;
    bsp_backlight_set_duty((uint16_t)duty);

    /* 标题与百分比同一行，滑块单独一行并水平居中 */
    lv_obj_t *bl_title = lv_label_create(tab_power);
    lv_label_set_text(bl_title, "背光亮度");
    lv_obj_set_style_text_color(bl_title, lv_color_hex(0xA0AEC0), 0);
    lv_obj_align(bl_title, LV_ALIGN_BOTTOM_LEFT, 0, -52);

    lv_obj_t *bl_slider = lv_slider_create(tab_power);
    lv_obj_set_width(bl_slider, 260);
    lv_slider_set_range(bl_slider, kBacklightMin, kBacklightMax);
    lv_slider_set_value(bl_slider, duty, LV_ANIM_OFF);
    lv_obj_align(bl_slider, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_add_event_cb(bl_slider, backlight_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    s_bl_label = lv_label_create(tab_power);
    lv_label_set_text_fmt(s_bl_label, "%d%%", duty - (kBacklightMin - 1));
    lv_obj_set_style_text_color(s_bl_label, lv_color_hex(0xE2E8F0), 0);
    lv_obj_align(s_bl_label, LV_ALIGN_BOTTOM_RIGHT, 0, -52);

    lv_obj_t *tx_btn = add_button(tab_radio, "持续发送", tx_cb,
                                  LV_EVENT_VALUE_CHANGED);
    lv_obj_add_flag(tx_btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(tx_btn, lv_color_hex(0xDD6B20), LV_STATE_CHECKED);

    s_tx_lbl = lv_label_create(tab_radio);
    lv_label_set_text(s_tx_lbl, "TX: 仅接收");
    lv_obj_set_style_text_color(s_tx_lbl, lv_color_hex(0xA0AEC0), 0);
    lv_obj_align(s_tx_lbl, LV_ALIGN_BOTTOM_RIGHT, -8, -18);

    add_button(tab_audio, "蜂鸣", beep_cb);

    lv_obj_t *slider = lv_slider_create(tab_audio);
    lv_obj_set_width(slider, 140);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 80, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_RIGHT, -4, -20);
    lv_obj_add_event_cb(slider, volume_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    bsp_audio_set_volume(80);

    apply_chinese_font(scr);

    s_state_lock = xSemaphoreCreateMutex();
    if (xTaskCreatePinnedToCore(radio_test_task, "radio_test", 8192,
                                nullptr, 2, nullptr, 0) != pdPASS) {
        Serial.println("[UI] radio test task create failed");
    }

    lv_timer_create(update_cb, 1000, nullptr);
}
