/**
 * bsp_pins.h — 板级硬件参数的唯一来源
 *
 * 规则（见 AGENTS.md / docs/bsp-api.md）：
 *   1. 应用代码不得出现裸 GPIO 数字，统一引用本文件。
 *   2. 驱动代码优先 include 本文件。
 *   3. 发生冲突时优先级：实测结果 > 本文件 > 驱动实现 > 架构文档 > README。
 *   4. 改引脚时同时更新本文件与 docs/hardware/。
 *
 * 板卡：ESP32-S3-WROOM-1U N16R8（16 MB Flash / 8 MB OPI PSRAM）
 * GPIO 扩展：GPS LDO EN = TCA9535 P02，总电源 = P03
 */
#ifndef BSP_PINS_H
#define BSP_PINS_H

/* ── 板卡与存储 ─────────────────────────────────────────────── */
#define BSP_BOARD_NAME           "GUACHAT 2G4"
#define BSP_MCU_NAME             "ESP32-S3"
#define BSP_FLASH_SIZE_MB        16
#define BSP_PSRAM_SIZE_MB        8

/* ── 显示：ST7796，SPI2，320x480，BGR，反显 ─────────────────── */
#define BSP_LCD_WIDTH            320
#define BSP_LCD_HEIGHT           480
#define BSP_LCD_SPI_HOST         SPI2_HOST
#define BSP_LCD_PIN_MOSI         13
#define BSP_LCD_PIN_SCLK         14
#define BSP_LCD_PIN_DC           21
#define BSP_LCD_PIN_CS           (-1)   /* 硬件固定选中 */
#define BSP_LCD_PIN_RST          (-1)   /* 由 TCA9535 P16 控制 */
#define BSP_LCD_SPI_WRITE_HZ     80000000
#define BSP_LCD_SPI_READ_HZ      20000000

/* 背光：GPIO46，LEDC ch0，10 kHz，11 bit；安全上限 170/2047 ≈ 8.3% */
#define BSP_LCD_PIN_BL           46
#define BSP_LCD_BL_LEDC_CH       0
#define BSP_LCD_BL_LEDC_HZ       10000
#define BSP_LCD_BL_LEDC_BITS     11
#define BSP_LCD_BL_DUTY_SAFE_MAX 170

/* ── 电池：开源硬件单节 18650，4.4 V / 5200 mAh（MAX17055） ──
 * 推荐 4.4 V / 5200 mAh 电池：建议应用层在低于 3.40 V 时触发自动关机，
 * 基线取 3.30 V；3.40 V 以下剩余容量已经很少。关机后 HT7125S 仍可为
 * GPS + RTC 提供超过 30 天的备用电源。BSP 只提供电压读数，关机策略由
 * 应用层实现。 */
#define BSP_BATTERY_DESIGN_MAH   5200
#define BSP_BATTERY_MIN_MV       3300   /* 建议 <3.40 V；基线自动关机电压 */
#define BSP_BATTERY_MAX_MV       4400   /* 满电电压 */
#define BSP_BATTERY_EMPTY_MV     3000   /* 空电电压（SOC 模型） */

/* ── I2C0：传感器 / 电源 / IO 扩展器，100 kHz ───────────────── */
#define BSP_I2C0_SDA             2
#define BSP_I2C0_SCL             1
#define BSP_I2C0_HZ              100000

#define BSP_I2C_ADDR_TCA9535     0x20
#define BSP_I2C_ADDR_QMC5883P    0x2C
#define BSP_I2C_ADDR_MAX17055    0x36
#define BSP_I2C_ADDR_SHT30       0x44
#define BSP_I2C_ADDR_BMP581      0x47
#define BSP_I2C_ADDR_PCF8563T    0x51
#define BSP_I2C_ADDR_DRV2605L    0x5A
#define BSP_I2C_ADDR_LSM6DS3TRC  0x6A

/* ── I2C1：触摸 FT6336U，400 kHz ───────────────────────────── */
#define BSP_I2C1_SDA             47
#define BSP_I2C1_SCL             48
#define BSP_I2C1_HZ              400000
#define BSP_I2C_ADDR_FT6336U     0x38
#define BSP_TOUCH_PIN_INT        45
#define BSP_TOUCH_PIN_RST        (-1)   /* 由 TCA9535 P15 控制 */

/* ── TCA9535 IO 扩展器（I2C0 0x20） ────────────────────────── */
#define BSP_TCA9535_PIN_INT      3
/* 引脚索引按“端口.位”编码：P10 = 端口1 位0 = 8 */
#define BSP_TCA9535_P00          0      /* 音量减（低有效） */
#define BSP_TCA9535_P01          1      /* 音量加（低有效） */
#define BSP_TCA9535_P02          2      /* GPS LDO EN（高=开） */
#define BSP_TCA9535_P03          3      /* 总电源（高=运行） */
#define BSP_TCA9535_P10          8      /* 硬件版本脚（输入） */
#define BSP_TCA9535_P14          12     /* SHT30 nRESET */
#define BSP_TCA9535_P15          13     /* 触摸 RST */
#define BSP_TCA9535_P16          14     /* 显示屏 RST */
#define BSP_TCA9535_P17          15     /* TF 卡 CD（低=已插入） */

/* ── TF 卡：SDMMC 1 线，40 MHz ─────────────────────────────── */
#define BSP_SD_PIN_CLK           10
#define BSP_SD_PIN_CMD           11
#define BSP_SD_PIN_D0            9
#define BSP_SD_MOUNT_POINT       "/sd"

/* ── 音频：I2S0 全双工，默认 8 kHz / 32 bit / 立体声 ────────── */
#define BSP_I2S_PIN_BCLK         38
#define BSP_I2S_PIN_LRC          39
#define BSP_I2S_PIN_DIN          40     /* 麦克风 → ESP32 */
#define BSP_I2S_PIN_DOUT         41     /* ESP32 → 功放 */
#define BSP_I2S_PIN_SD_MODE      42
#define BSP_I2S_SAMPLE_RATE      8000   /* 默认值；运行时可用 bsp_audio_set_sample_rate() 改 */
#define BSP_I2S_BITS             32

/* ── LoRa：E28-2G4M20S / SX1280，SPI3 ──────────────────────── */
#define BSP_LORA_SPI_HOST        SPI3_HOST
#define BSP_LORA_PIN_MISO        4
#define BSP_LORA_PIN_MOSI        5
#define BSP_LORA_PIN_SCK         6
#define BSP_LORA_PIN_NSS         7
#define BSP_LORA_PIN_DIO1        8
#define BSP_LORA_PIN_RXEN        15
#define BSP_LORA_PIN_TXEN        16
#define BSP_LORA_PIN_NRESET      17
#define BSP_LORA_PIN_BUSY        18
#define BSP_LORA_FREQ_MIN_MHZ    2402
#define BSP_LORA_FREQ_MAX_MHZ    2482
#define BSP_LORA_POWER_RECOMMENDED_DBM 7  /* 默认/推荐值；与 WiFi 并发仅验证过此功率，超过时固件只提示 */
#define BSP_LORA_POWER_MAX_DBM   20     /* E28-2G4M20S 模块硬件额定上限，不是策略限制 */

/* ── GPS：MAX-M10S，UART0 ──────────────────────────────────── */
#define BSP_GPS_UART_NUM         0
#define BSP_GPS_PIN_TX           43     /* ESP32 TX → GPS RX */
#define BSP_GPS_PIN_RX           44     /* ESP32 RX ← GPS TX */
#define BSP_GPS_BAUD             230400

/* ── 按键 / USB ────────────────────────────────────────────── */
#define BSP_BOOT_BTN_PIN         0      /* BOOT / 锁屏键，低=按下 */
#define BSP_USB_PIN_DM           19
#define BSP_USB_PIN_DP           20

#endif /* BSP_PINS_H */
