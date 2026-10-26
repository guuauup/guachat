// ============================================================
// TFT_eSPI User_Setup for ST7796 320x480 on ESP32-S3
// 引脚、分辨率与 SPI 频率统一来自 src/bsp/bsp_pins.h
// 显示屏复位由 TCA9535 控制（TFT_RST = -1）
// ============================================================

#include "../../src/bsp/bsp_pins.h"

#define USER_SETUP_INFO "ST7796_320x480_ESP32S3"

// 驱动选择
#define ST7796_DRIVER

// 颜色顺序 (ST7796 通常为 BGR)
#define TFT_RGB_ORDER TFT_BGR

// 反转
#define TFT_INVERSION_ON

// 分辨率
#define TFT_WIDTH  BSP_LCD_WIDTH
#define TFT_HEIGHT BSP_LCD_HEIGHT

// SPI 引脚
#define TFT_MISO (-1)
#define TFT_MOSI BSP_LCD_PIN_MOSI
#define TFT_SCLK BSP_LCD_PIN_SCLK
#define TFT_CS   BSP_LCD_PIN_CS
#define TFT_DC   BSP_LCD_PIN_DC
#define TFT_RST  BSP_LCD_PIN_RST
#define TFT_BL   (-1)

// 触摸 CS（不使用 SPI 触摸，用 I2C 的 FT6336U）
#define TOUCH_CS (-1)

// SPI 频率 (Hz)
#define SPI_FREQUENCY       BSP_LCD_SPI_WRITE_HZ
#define SPI_READ_FREQUENCY  BSP_LCD_SPI_READ_HZ

// 字体
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4

// 不使用 SPIFFS 字库
#define SMOOTH_FONT

#define TFT_SPI_HOST BSP_LCD_SPI_HOST
