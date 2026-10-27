# BSP API

> 应用层只通过本页的接口访问硬件。所有引脚/地址常量在
> [`../src/bsp/bsp_pins.h`](../src/bsp/bsp_pins.h)，不要写裸 GPIO。
>
> 线程总原则：**LVGL 只能在 `ui_task` 里调用**；BSP 的阻塞接口不要放进 UI 回调。
> 详细规则见 [`concurrency.md`](concurrency.md)。

---

## 0. 初始化顺序

`guachat_dev` 的 `setup()` 已按以下顺序完成，新应用不要在中间插入会破坏顺序的代码：

```cpp
TCA9535Manager::getInstance()->begin();   // I2C0 总线 + IO 扩展器
bsp_display_init();                       // 屏幕硬件，LVGL 之前
lv_init();
bsp_display_lvgl_init();                  // LVGL 显示驱动
bsp_input_init();                         // LVGL 触摸设备
dev_ui_build();                           // 你的界面（并应用默认背光）

// Core 0，后台
bsp_gps_init();
bsp_haptic_init();                        // DRV2605L
bsp_haptic_boot_feedback();               // 开机震动一次
bsp_power_init();
bsp_storage_init();
bsp_sensors_init();
bsp_radio_init();
bsp_audio_init();
```

- `bsp_display_lvgl_init()` / `bsp_input_init()` 必须在 `lv_init()` 之后。
- `bsp_power_init()` / `bsp_sensors_init()` / `bsp_haptic_init()` 依赖 I2C0 已初始化。
- 所有 `bsp_*_init()` 都允许失败，应用必须能降级运行。
- 开机震动是硬件提示，不是应用功能；见第 9 节。

---

## 1. `bsp_display.h` — 显示与背光

```c
void bsp_display_init(void);
void bsp_display_lvgl_init(void);
void bsp_backlight_set(uint8_t percent);       // 0..100，内部钳位
void bsp_backlight_set_duty(uint16_t duty);   // 0..2047，内部钳位
void bsp_backlight_on(bool on);
```

| 项目 | 说明 |
| --- | --- |
| 屏幕 | ST7796，320×480，SPI2，BGR，反显 |
| 缓冲 | PSRAM 双缓冲，整屏 480 行（每块 300 KB，共 600 KB） |
| 背光 | GPIO46，LEDC 10 kHz / 11 bit；**安全上限 170/2047 ≈ 8.3%** |
| 线程 | 初始化在 `setup()`；`flush` 回调在 `ui_task` |

`bsp_backlight_set(100)` 会被钳到安全上限。不要绕过 BSP 直接 `ledcWrite`。

自检界面的“电源”页带有背光调节：

- 滑块范围 `71..170`，直接对应 LEDC 占空比；标签显示 `duty - 70`，即 `1..100%`。
- 开机默认 `100`（约 30%）。测试固件不做持久化，重启回到默认值。
- 拖动时实时调光，不写 NVS。

---

## 2. `bsp_input.h` — 触摸

```c
void     bsp_input_init(void);
bool     bsp_touch_available(void);
uint32_t bsp_touch_last_activity_ms(void);
```

- FT6336U，I2C1（47/48，400 kHz），INT 45，RST 由 TCA9535 P15。
- `bsp_input_init()` 注册 LVGL 指针设备；之后触摸事件由 `lv_timer_handler()` 派发。
- 需要“无操作息屏”时，用 `bsp_touch_last_activity_ms()` 与 `millis()` 比较。

---

## 3. `bsp_power.h` — 电池

```c
typedef struct {
    bool     valid;
    uint16_t voltage_mv;
    int16_t  current_ma;   // 正=充电，负=放电
    uint8_t  soc_percent;  // 0..100
    uint16_t remaining_mah;
    uint16_t design_mah;
    float    temperature_c;
    bool     charging;     // current_ma > 50
} bsp_power_info_t;

bool bsp_power_init(void);
bool bsp_power_read(bsp_power_info_t *out);
```

- MAX17055，I2C0 0x36；开源硬件为单节 18650（4.4 V / 5200 mAh），初始化时写入
  `BSP_BATTERY_DESIGN_MAH` 与 `BSP_BATTERY_EMPTY_MV`（见 `bsp_pins.h`）。
- 推荐电池（4.4 V / 5200 mAh）建议应用层在低于 3.40 V 时触发自动关机；
  `BSP_BATTERY_MIN_MV` 基线为 3.30 V。3.40 V 以下剩余容量已经很少。关机后
  HT7125S 仍可为 GPS + RTC 提供 >30 天备用电源。BSP 本身不实现关机策略。
- `bsp_power_read()` 内部持 I2C 锁，可在任意任务调用；建议 1 Hz。
- 失败时返回 `false`，`out->valid=false`，界面应显示 `n/a`。

---

## 4. `bsp_storage.h` — TF 卡

```c
typedef struct {
    bool     mounted;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint32_t bus_freq_hz;
    char     card_type[16];
} bsp_storage_info_t;

bool bsp_storage_init(void);
void bsp_storage_info(bsp_storage_info_t *out);
void bsp_storage_unmount(void);
```

- SDMMC 1 线：CLK 10 / CMD 11 / D0 9；挂载点 `/sd`。
- `bsp_storage_init()` 可能重试，**不要在 UI 任务里调用**；在 `hw_init_task` 或自己的后台任务里做。
- 文件访问用标准 `SD` / `FILE*`，但必须加 `SdLockGuard`（见 `src/storage/SDCardManager.h`）。
- 热插拔前调用 `bsp_storage_unmount()`。

示例：

```cpp
#include "storage/SDCardManager.h"

{
    SdLockGuard lock;
    FILE *f = fopen("/sd/demo.txt", "w");
    if (f) {
        fputs("hello\n", f);
        fclose(f);
    }
}
```

---

## 5. `bsp_sensors.h` — 传感器集合

```c
typedef struct {
    bool mag_ok, imu_ok, env_ok, baro_ok, rtc_ok;

    float accel_mps2[3];
    float gyro_rads[3];
    float mag[3];          // 已校准；无校准参数时为原始 counts
    float heading_deg;     // 0..360
    float pitch_deg, roll_deg;

    float temperature_c, humidity_pct, dew_point_c;
    float pressure_pa;

    uint16_t rtc_year;
    uint8_t  rtc_month, rtc_day, rtc_hour, rtc_minute, rtc_second;
    bool     rtc_voltage_low;
} bsp_sensors_info_t;

bool bsp_sensors_init(void);
bool bsp_sensors_read(bsp_sensors_info_t *out);
```

| 设备 | 地址 | 说明 |
| --- | --- | --- |
| QMC5883P | 0x2C | 地磁；缺失时 `mag_ok=false`，其余继续 |
| LSM6DS3TRC | 0x6A | 六轴 IMU |
| SHT30 | 0x44 | 温湿度/露点 |
| BMP581 | 0x47 | 气压，`pressure_pa` 单位 **Pa** |
| PCF8563T | 0x51 | RTC |

- `bsp_sensors_read()` 会做多次 I2C 事务（几 ms 到十几 ms），**不要每帧调用**；1 Hz 足够。
- 该函数内部经 `i2cBusLock()`，可从任意任务调用，但不要和其他传感器调用方并发刷屏。
- BMP581 的 Adafruit 库原始单位是 hPa，BSP 已乘 100 统一成 Pa。

---

## 6. `bsp_radio.h` — LoRa

```c
typedef struct {
    bool    ready;
    float   freq_mhz;
    uint8_t sf;
    float   bw_khz;
    int8_t  power_dbm;
} bsp_radio_info_t;

bool bsp_radio_init(void);
bool bsp_radio_set_params(float freq_mhz, uint8_t sf, float bw_khz, int8_t power_dbm);
bool bsp_radio_start_rx(void);
bool bsp_radio_send(const uint8_t *data, size_t len);
bool bsp_radio_available(void);
int  bsp_radio_read(uint8_t *buf, size_t max_len, size_t *out_len);
int  bsp_radio_receive(uint8_t *buf, size_t max_len, uint32_t timeout_ms, size_t *out_len);
void bsp_radio_info(bsp_radio_info_t *out);
```

- 默认：2405 MHz / SF10 / BW812.5 / CR4/5 / +7 dBm（推荐值）。
- 频率由 `RadioConfig.h` 钳位在 2402–2482 MHz；功率最高钳位到 E28-2G4M20S
  的 20 dBm 硬件上限。高于 7 dBm 不会阻止调用，只在串口打印风险提示；
  `bsp_radio_set_params()` 可切换单频参数。
- `bsp_radio_init()` 成功后自动进入连续 RX。
- **`bsp_radio_send()` 和 `bsp_radio_receive()` 是阻塞调用**，不要从 `ui_task` 调用。
- UI 里用非阻塞组合：

```cpp
if (bsp_radio_available()) {
    uint8_t buf[64];
    size_t len = 0;
    if (bsp_radio_read(buf, sizeof(buf), &len) == 0) {
        // 处理 len 字节
    }
    bsp_radio_start_rx();   // 读完必须重新武装 RX
}
```

- `bsp_radio_send()` 之后也要 `bsp_radio_start_rx()`。
- 需要周期发送时，把发送循环放进独立任务，不要阻塞 `ui_task`；
  可参考 `src/dev/dev_ui.cpp` 的 `radio_test_task`。
- 没有第二台设备时，只能验证初始化与 TX 返回成功，RX 必须标为 `Unverified`。

---

## 7. `bsp_gps.h` — GPS

```c
bool bsp_gps_init(void);
const gps_data_t *bsp_gps_get(void);   // 只读快照
bool bsp_gps_alive(void);              // 是否收到过 NAV-PVT
```

`gps_data_t` 主要字段（完整定义见 `src/gps/ubx_parser.h`）：

| 字段 | 含义 |
| --- | --- |
| `lastPvtMs` | 最近一次 NAV-PVT 的 `millis()`，0 表示还没收到 |
| `fixType` | 0=无定位，2=2D，3=3D |
| `numSV` / `numSvs` | 使用中 / 可见卫星数 |
| `lat` / `lon` | 度 |
| `hMSL` | 海平面高，mm |
| `gSpeed` | 地速，mm/s |
| `hAcc` / `vAcc` | 水平/垂直精度，mm |
| `year..sec` | UTC 时间 |
| `timeValid` | 日期与时间是否有效 |

- `bsp_gps_init()` 会启动 Core 0 的 `gps` 任务，先自动探测波特率再配置 UBX。
- 返回的指针指向全局快照，**不要写**；读取时允许拿到上一帧的一致数据。
- 室内通常无定位，测试时说明环境。

---

## 8. `bsp_audio.h` — 音频

```c
bool     bsp_audio_init(void);
void     bsp_audio_set_enabled(bool en);           // 功放静音/使能
void     bsp_audio_set_volume(uint8_t percent);    // 0..100
uint8_t  bsp_audio_get_volume(void);
bool     bsp_audio_set_sample_rate(uint32_t hz);   // 8000..96000
uint32_t bsp_audio_get_sample_rate(void);
void     bsp_audio_beep(uint16_t freq_hz, uint16_t dur_ms);
size_t   bsp_audio_write_pcm16(const int16_t *pcm, size_t frames, uint32_t timeout_ms);
size_t   bsp_audio_read_pcm16(int16_t *out, size_t max_frames, uint32_t timeout_ms);
```

- I2S0 全双工：默认 8 kHz / 32 bit / 立体声；采样率可用
  `bsp_audio_set_sample_rate()` 在运行时调整（默认值见 `BSP_I2S_SAMPLE_RATE`）。
- 应用层只面对 **PCM16 单声道**：`write` 自动复制到左右声道并应用软件音量，
  `read` 自动取 32-bit 左对齐样本的高 16 bit。
- `bsp_audio_beep()` **阻塞 `dur_ms`**；短提示音可以，长音不要放 UI 回调。
- `bsp_audio_write_pcm16()` / `bsp_audio_read_pcm16()` 返回实际帧数；
  `timeout_ms=0` 为非阻塞。长 PCM 播放请放在独立任务里，避免卡住 UI。
- 不要直接调用 `i2s_*` 或包含 `I2S/MAX98357.h`；播放、录音都走 BSP，才能共享互斥与音量。
- 麦克风峰值示例：

```cpp
int16_t pcm[128];
size_t n = bsp_audio_read_pcm16(pcm, 128, 20);
int peak = 0;
for (size_t i = 0; i < n; ++i) {
    int v = pcm[i] < 0 ? -pcm[i] : pcm[i];
    if (v > peak) peak = v;
}
```

- 播放自定义 PCM（同样是单声道 PCM16）：

```cpp
bsp_audio_set_sample_rate(16000);          // 同时影响播放与录音；建议在两者都空闲时调用
bsp_audio_write_pcm16(pcm, frames, 100);   // 阻塞到写完或 100 ms 超时
```

---

## 9. `bsp_haptic.h` — 触觉（DRV2605L LRA）

```c
bool bsp_haptic_init(void);
bool bsp_haptic_ready(void);
bool bsp_haptic_settings_loaded(void);   // NVS 设置是否已加载

bool bsp_haptic_play(uint8_t effect);    // 固定默认提醒电压，非阻塞
bool bsp_haptic_alert(void);             // 按用户设置的效果 + 强度播放
bool bsp_haptic_preview(uint8_t effect); // 按用户强度试听指定效果
bool bsp_haptic_key_click(void);         // 低压触控反馈

bool bsp_haptic_recalibrate(void);       // 重新校准，阻塞 1–3 秒

uint8_t bsp_haptic_get_alert_level(void);          // 0..10，0 = 关闭
bool    bsp_haptic_set_alert_level(uint8_t level);
uint8_t bsp_haptic_get_alert_effect(void);         // 0 = 关闭
bool    bsp_haptic_set_alert_effect(uint8_t effect);
bool    bsp_haptic_get_key_feedback(void);
void    bsp_haptic_set_key_feedback(bool enabled);

void bsp_haptic_boot_feedback(void);     // 开机震动一次，只生效一次
void bsp_haptic_tick(void);              // 周期调用；播放完回 standby
void bsp_haptic_stop(void);
```

- 本机**没有电源指示灯**。`main.cpp` 在 `hw_init_task` 中调用
  `bsp_haptic_init()` + `bsp_haptic_boot_feedback()`，用一次 Strong Click
  提示用户设备已经开机。**重写界面时保留这次震动**，或提供等效的开机提示。
- 常用效果号：`BSP_HAPTIC_EFFECT_STRONG_CLICK_100`(1)、`SHARP_CLICK_100`(4)、
  `DOUBLE_CLICK_100`(10)、`TRIPLE_CLICK_100`(12)、`STRONG_BUZZ_100`(14)、
  `LONG_DOUBLE_100`(44)、`BUZZ_2_80`(48)、`KEY_CLICK`(26)。
  完整 123 个效果见 DRV2605 数据手册 §11.2（Library 6 = LRA）。
- 用户设置保存在 NVS 命名空间 `drv2605`，键名与原 GUACHAT-2G4 兼容：
  `alert_lvl`(0..10)、`alert_eff`(0..123，0=关)、`kb_on`(触控反馈)。默认
  强度 5、效果 48（Buzz 2 80%）、触控反馈开。
- 强度 0 时 `bsp_haptic_alert()` / `bsp_haptic_preview()` 不播放，
  **开机震动也会关闭**（与原机一致）。强度 1..10 映射到 RATEDV 46..97
  （约 1.0–2.1 V）；默认 5 对应 `BSP_HAPTIC_ALERT_RV`(0x44)。
- `bsp_haptic_play(effect)` 固定使用 `BSP_HAPTIC_ALERT_RV`，不读取用户设置；
  需要遵循用户设置时用 `bsp_haptic_alert()` 或 `bsp_haptic_preview()`。
- 触控反馈：在按钮 `LV_EVENT_CLICKED`、滑块/roller/switch 的
  `LV_EVENT_VALUE_CHANGED` 回调里调用 `bsp_haptic_key_click()`。它播放效果 26
  （Sharp Tick 3 60%）并使用空闲低压 `BSP_HAPTIC_IDLE_RV`(0x0A)；开关关闭时
  立即返回 `false`。`src/dev/dev_ui.cpp` 的 `add_haptic_feedback()` 是递归绑定示例。
- `bsp_haptic_recalibrate()` 会阻塞 1–3 秒并让马达嗡鸣，**只能在后台任务里调用**；
  成功后覆盖 NVS 中的 `comp`/`emp`/`fb`。校准期间其它播放/触控反馈调用会直接
  跳过（不阻塞 UI），`bsp_haptic_tick()` 也暂时跳过。
- `bsp_haptic_play()` / `bsp_haptic_alert()` 立即返回；必须周期调用
  `bsp_haptic_tick()`（建议 100–500 ms），播放完成或超过 3 秒后驱动自动恢复
  空闲电压并进入 standby。基线在 `loop()` 中每 100 ms 调用一次。
- 初始化会读取 NVS 中的校准数据；没有时执行 LRA 自动校准并保存到命名空间
  `drv2605`。首次启动（NVS 无校准）会多花 1–2 秒，并伴随校准震动。
  `full.bin` 从 `0x0` 刷写不保证清空 NVS；需要从零校准时先显式擦除。
- 触觉初始化失败（0x5A 无应答）只降级为“不震动”，不影响其它 BSP 和外设。
- 不要直接写 DRV2605L 寄存器；电压、效果号和 LRA 参数在
  `bsp_pins.h` 的 `BSP_HAPTIC_*` 中定义，设置默认值在 `bsp_haptic.h`。

触控反馈示例：

```cpp
lv_obj_add_event_cb(btn, [](lv_event_t *) {
    bsp_haptic_key_click();
}, LV_EVENT_CLICKED, nullptr);
```

后台重新校准示例：

```cpp
static void haptic_cal_task(void *)
{
    const bool ok = bsp_haptic_recalibrate();
    // 只写快照/标志，由 UI 定时器读取；不要在这个任务里调用 lv_*
    vTaskDelete(nullptr);
}
```

---

## 10. `bsp_i2c.h` — 诊断

```c
void bsp_i2c_scan(void);
bool bsp_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *value);
```

仅用于调试/自检。应用代码不要用它替代设备 Manager。

---

## 11. 一个最小应用骨架

```cpp
#include <Arduino.h>
#include <lvgl.h>

#include "bsp/bsp_display.h"
#include "bsp/bsp_input.h"
#include "bsp/bsp_pins.h"
#include "I2C0/TCA9535.h"

static lv_obj_t *s_label;

static void ui_task(void *)
{
    for (;;) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    s_label = lv_label_create(scr);
    lv_label_set_text(s_label, "Hello");
    lv_obj_center(s_label);
}

void setup()
{
    Serial.begin(115200);

    TCA9535Manager::getInstance()->begin();
    bsp_display_init();
    lv_init();
    bsp_display_lvgl_init();
    bsp_input_init();

    build_ui();
    bsp_backlight_set(60);

    xTaskCreatePinnedToCore(ui_task, "ui", 8192, nullptr, 5, nullptr, 1);
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
```

实际基线还会在 `hw_init_task` 中调用 `bsp_haptic_init()` +
`bsp_haptic_boot_feedback()`，并在 `loop()` 中调用 `bsp_haptic_tick()`；
这是无电源指示灯时的开机提示，重写应用时保留。

更多可用接口见 `src/bsp/*.h`。如果你需要新的 BSP 能力，先加在 `src/bsp/`，
再在 `bsp_pins.h` 登记硬件参数，最后更新本文档。
