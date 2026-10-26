/**
 * bsp_gps.cpp — GPS 串口任务
 */
#include "bsp_gps.h"
#include "bsp_pins.h"

#include <Arduino.h>

namespace {

bool s_started = false;

void gps_task(void *arg)
{
    (void)arg;

    /* 自动检测 230400 / 115200 / 38400 并配置 MAX-M10S */
    ubx_configure();

    for (;;) {
        while (Serial0.available()) {
            ubx_encode((uint8_t)Serial0.read());
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

}  // namespace

bool bsp_gps_init(void)
{
    if (s_started) {
        return true;
    }

    Serial0.setRxBufferSize(2048);
    Serial0.begin(BSP_GPS_BAUD, SERIAL_8N1, BSP_GPS_PIN_RX, BSP_GPS_PIN_TX);

    BaseType_t ok = xTaskCreatePinnedToCore(
        gps_task, "gps", 4096, nullptr, 1, nullptr, 0);
    if (ok != pdPASS) {
        Serial.println("[BSP] gps: task create failed");
        return false;
    }

    s_started = true;
    Serial.println("[BSP] gps: task started");
    return true;
}

const gps_data_t *bsp_gps_get(void)
{
    return &g_gpsData;
}

bool bsp_gps_alive(void)
{
    return g_gpsData.lastPvtMs != 0;
}
