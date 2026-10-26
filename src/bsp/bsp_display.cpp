/**
 * bsp_display.cpp — ST7796 + LVGL 的最小显示/背光实现
 */
#include "bsp_display.h"
#include "bsp_pins.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <esp_heap_caps.h>

#include "I2C0/TCA9535.h"

/* 整屏双缓冲：320 × 480 × 2 B = 300 KB/块，两块共 600 KB PSRAM。
   如需省 PSRAM，可编译时用 -DBSP_LV_BUF_LINES=60 改成分块。 */
#ifndef BSP_LV_BUF_LINES
#define BSP_LV_BUF_LINES BSP_LCD_HEIGHT
#endif

static TFT_eSPI s_tft;
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t *s_buf1 = nullptr;
static lv_color_t *s_buf2 = nullptr;
static bool s_backlight_on = true;
static uint16_t s_backlight_duty = 100;

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    const uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    const uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

    s_tft.startWrite();
    s_tft.setAddrWindow(area->x1, area->y1, w, h);
    s_tft.pushColors((uint16_t *)color_p, w * h, true);
    s_tft.endWrite();

    lv_disp_flush_ready(disp);
}

void bsp_display_init(void)
{
    TCA9535Manager::getInstance()->displayReset();

    s_tft.begin();
    s_tft.setRotation(0);
    s_tft.setSwapBytes(true);
    s_tft.fillScreen(TFT_BLACK);

    ledcSetup(BSP_LCD_BL_LEDC_CH, BSP_LCD_BL_LEDC_HZ, BSP_LCD_BL_LEDC_BITS);
    ledcAttachPin(BSP_LCD_PIN_BL, BSP_LCD_BL_LEDC_CH);
    bsp_backlight_set(3);

    Serial.printf("[BSP] display: ST7796 %dx%d, SPI2 MOSI=%d SCLK=%d DC=%d\n",
                  BSP_LCD_WIDTH, BSP_LCD_HEIGHT,
                  BSP_LCD_PIN_MOSI, BSP_LCD_PIN_SCLK, BSP_LCD_PIN_DC);
}

void bsp_display_lvgl_init(void)
{
    const size_t buf_bytes =
        (size_t)BSP_LCD_WIDTH * BSP_LV_BUF_LINES * sizeof(lv_color_t);

    s_buf1 = (lv_color_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    s_buf2 = (lv_color_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);

    if (s_buf1 && s_buf2) {
        lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2,
                              BSP_LCD_WIDTH * BSP_LV_BUF_LINES);
        Serial.printf("[BSP] LVGL: double buffer, %d lines each\n", BSP_LV_BUF_LINES);
    } else if (s_buf1) {
        lv_disp_draw_buf_init(&s_draw_buf, s_buf1, nullptr,
                              BSP_LCD_WIDTH * BSP_LV_BUF_LINES);
        Serial.printf("[BSP] LVGL: single buffer, %d lines\n", BSP_LV_BUF_LINES);
    } else {
        Serial.println("[BSP] ERROR: LVGL buffer allocation failed");
        while (true) {
            delay(1000);
        }
    }

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = BSP_LCD_WIDTH;
    disp_drv.ver_res = BSP_LCD_HEIGHT;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&disp_drv);
}

void bsp_backlight_set_duty(uint16_t duty)
{
    if (duty > BSP_LCD_BL_DUTY_SAFE_MAX) {
        duty = BSP_LCD_BL_DUTY_SAFE_MAX;
    }
    s_backlight_duty = duty;
    if (s_backlight_on) {
        ledcWrite(BSP_LCD_BL_LEDC_CH, duty);
    }
}

void bsp_backlight_set(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    const uint32_t max_duty = (1u << BSP_LCD_BL_LEDC_BITS) - 1u;
    bsp_backlight_set_duty((uint16_t)(max_duty * percent / 100u));
}

void bsp_backlight_on(bool on)
{
    s_backlight_on = on;
    ledcWrite(BSP_LCD_BL_LEDC_CH, on ? s_backlight_duty : 0);
}
