/**
 * main.cpp — guachat_dev 最小开发基线入口
 *
 * 这是给 AI Agent 和二次开发者使用的干净起点：
 *   - 只初始化 BSP（显示 / 触摸）与 LVGL
 *   - 不依赖外部 UI 生成器，也不包含应用业务代码
 *   - 用户应用应从这里的 UI 结构之外重新设计自己的界面
 *
 * 任务模型：LVGL 只在 uiTask（Core 1）里调用；其他任务不得直接碰 LVGL。
 */
#include <Arduino.h>
#include <lvgl.h>

#include "bsp/bsp_audio.h"
#include "bsp/bsp_buttons.h"
#include "bsp/bsp_display.h"
#include "bsp/bsp_gps.h"
#include "bsp/bsp_haptic.h"
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_input.h"
#include "bsp/bsp_pins.h"
#include "bsp/bsp_power.h"
#include "bsp/bsp_radio.h"
#include "bsp/bsp_sensors.h"
#include "bsp/bsp_storage.h"
#include "dev/dev_ui.h"
#include "I2C0/TCA9535.h"

/*
 * 硬件初始化放在独立任务里：显示/触摸先出画面，TF 卡重试、GPS 配置等
 * 耗时操作不会让屏幕黑着等。所有硬件初始化完成后任务自行退出。
 */
static void hw_init_task(void *arg)
{
    (void)arg;

    /* 没有电源指示灯：先初始化触觉并震动一次，提示用户设备已经开机 */
    bsp_haptic_init();
    bsp_haptic_boot_feedback();

    bsp_power_init();
    bsp_storage_init();
    bsp_i2c_scan();
    uint8_t id7e = 0;
    if (bsp_i2c_read_reg(0x7E, 0x00, &id7e)) {
        Serial.printf("[BSP] 0x7E reg0 = 0x%02X\n", id7e);
    }
    bsp_sensors_init();
    bsp_radio_init();
    bsp_audio_init();

    Serial.println("[BSP] hardware init done");
    vTaskDelete(nullptr);
}

static void ui_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        lv_timer_handler();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    delay(200);

    Serial.println();
    Serial.println("========================================");
    Serial.printf("  %s\n", BSP_BOARD_NAME);
    Serial.println("  guachat_dev baseline");
    Serial.println("========================================");

    if (!TCA9535Manager::getInstance()->begin()) {
        Serial.println("[BSP] WARN: TCA9535 not found; display/touch reset may fail");
    }

    bsp_buttons_init();   /* 三个可编程按键：BOOT + 音量加/减 */

    bsp_display_init();

    lv_init();
    bsp_display_lvgl_init();
    bsp_input_init();

    /* bsp_display_init() 先把亮度降到 3%；dev_ui_build() 会应用默认背光 */
    dev_ui_build();

    BaseType_t ok = xTaskCreatePinnedToCore(
        ui_task, "ui", 8192, nullptr, 5, nullptr, 1);
    if (ok != pdPASS) {
        Serial.println("[BSP] ERROR: failed to create ui task");
    }

    /* GPS 自己带后台任务；其余硬件在 Core 0 初始化，避免阻塞首帧 */
    bsp_gps_init();
    xTaskCreatePinnedToCore(hw_init_task, "hw_init", 8192, nullptr, 3,
                            nullptr, 0);

    Serial.println("[BSP] dev baseline ready");
}

void loop()
{
    // 所有 UI 工作都在 uiTask；Arduino loop 任务只做 BSP 低频维护。
    bsp_haptic_tick();
    vTaskDelay(pdMS_TO_TICKS(100));
}
