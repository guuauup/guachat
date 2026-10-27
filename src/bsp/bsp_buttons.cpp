/**
 * bsp_buttons.cpp — 三个可编程实体按键的读取
 *
 * 只做原始电平读取，不实现短按/长按/音量业务；消抖和动作由应用决定。
 */
#include "bsp_buttons.h"
#include "bsp_pins.h"

#include <Arduino.h>

#include "I2C0/TCA9535.h"

void bsp_buttons_init(void)
{
    pinMode(BSP_BOOT_BTN_PIN, INPUT_PULLUP);
}

bool bsp_buttons_read(bsp_buttons_state_t *out)
{
    if (!out) {
        return false;
    }

    *out = {};
    out->boot = (digitalRead(BSP_BOOT_BTN_PIN) == LOW);

    bool vol_down = false;
    bool vol_up = false;
    if (!TCA9535Manager::getInstance()->readButtons(vol_down, vol_up)) {
        return false;
    }

    out->vol_down = vol_down;
    out->vol_up = vol_up;
    out->vol_valid = true;
    return true;
}

bool bsp_button_pressed(bsp_button_t button)
{
    if (button == BSP_BUTTON_BOOT) {
        return digitalRead(BSP_BOOT_BTN_PIN) == LOW;
    }

    if (button != BSP_BUTTON_VOL_UP && button != BSP_BUTTON_VOL_DOWN) {
        return false;
    }

    bool vol_down = false;
    bool vol_up = false;
    if (!TCA9535Manager::getInstance()->readButtons(vol_down, vol_up)) {
        return false;
    }

    return button == BSP_BUTTON_VOL_UP ? vol_up : vol_down;
}
