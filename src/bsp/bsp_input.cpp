/**
 * bsp_input.cpp — FT6336U → LVGL 最小输入实现
 */
#include "bsp_input.h"
#include "bsp_pins.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <FT6336U.h>

#include "I2C0/TCA9535.h"

static FT6336U s_touch(BSP_I2C1_SDA, BSP_I2C1_SCL, BSP_TOUCH_PIN_INT, &Wire1);
static bool s_touch_ok = false;
static bool s_touch_enabled = true;
static uint32_t s_last_activity_ms = 0;

static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;

    if (s_touch_enabled && s_touch_ok && s_touch.available()) {
        data->state = LV_INDEV_STATE_PR;
        s_last_activity_ms = millis();

        int32_t x = s_touch.touchPoint.tp[0].x;
        int32_t y = s_touch.touchPoint.tp[0].y;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= BSP_LCD_WIDTH)  x = BSP_LCD_WIDTH - 1;
        if (y >= BSP_LCD_HEIGHT) y = BSP_LCD_HEIGHT - 1;
        data->point.x = (lv_coord_t)x;
        data->point.y = (lv_coord_t)y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void bsp_input_init(void)
{
    TCA9535Manager::getInstance()->touchReset();

    Serial.print("[BSP] touch: FT6336U...");
    if (!s_touch.begin()) {
        s_touch_ok = false;
        Serial.println(" NOT FOUND (touch disabled)");
        return;
    }

    s_touch_ok = true;
    s_touch_enabled = true;
    Serial.printf(" OK (chip=0x%02X fw=0x%02X)\n",
                  s_touch.read_chip_id(), s_touch.read_firmware_id());

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);
}

bool bsp_touch_available(void)
{
    return s_touch_ok;
}

uint32_t bsp_touch_last_activity_ms(void)
{
    return s_last_activity_ms;
}
