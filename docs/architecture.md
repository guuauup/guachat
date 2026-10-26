# 架构

> 本文回答：这个工程如何启动、任务怎么分工、内存怎么用、BSP 与应用的边界在哪。
> 引脚与总线参数见 [`hardware/硬件连接说明.md`](hardware/硬件连接说明.md)，
> API 细节见 [`bsp-api.md`](bsp-api.md)。

---

## 1. 构建环境

`platformio.ini` 只有一个环境：

| 环境 | 用途 | 编译内容 |
| --- | --- | --- |
| `guachat_dev` | AI / 二次开发起点 | `src/main.cpp` + `src/bsp/` + `src/dev/` + BSP 依赖的干净驱动 |

工程只包含 BSP、`src/dev/` 基线和依赖驱动，不在 `main` 上编译应用业务代码；
唯一入口是 `src/main.cpp`。

新增顶层源码目录默认会被编译；如果它有自己的 `setup()/loop()`，必须改成独立应用入口，
不要和 `src/main.cpp` 同时存在。

---

## 2. guachat_dev 启动顺序

```text
setup()  (Arduino loopTask)
  ├─ Serial.begin(115200)
  ├─ TCA9535Manager::begin()        // I2C0 总线 + IO 扩展器
  ├─ bsp_display_init()             // ST7796 复位、TFT_eSPI、背光 3%
  ├─ lv_init()
  ├─ bsp_display_lvgl_init()        // PSRAM 整屏双缓冲，注册显示驱动
  ├─ bsp_input_init()               // FT6336U + LVGL 指针设备
  ├─ dev_ui_build()                 // 纯 LVGL 界面 + 1 Hz 定时器，应用默认背光
  ├─ ui_task                         // Core 1, prio 5, 10 ms
  ├─ bsp_gps_init()                  // Core 0 gps 任务
  └─ hw_init_task                    // Core 0, prio 3, 一次性
        ├─ bsp_power_init()          // MAX17055
        ├─ bsp_storage_init()        // TF 卡挂载 /sd
        ├─ bsp_i2c_scan()
        ├─ bsp_sensors_init()        // Fusion/IMU/SHT30/BMP581/RTC
        ├─ bsp_radio_init()          // SX1280，随后进入连续 RX
        └─ bsp_audio_init()          // I2S 全双工 + 功放
```

设计意图：

- **先出画面**。显示/触摸初始化完成后立即进入 `ui_task`，耗时的 SD 重试、GPS 配置、
  传感器探测放到 Core 0，避免黑屏等待。
- **GPS 独立任务**。`ubx_configure()` 会自动探测波特率，可能耗时数秒。
- **UI 定时器**。`dev_ui.cpp` 的 1 Hz 定时器在 `ui_task` 的 `lv_timer_handler()` 中执行，
  所以可以安全调用 `lv_*`。

---

## 3. 任务模型

| 任务 | 核心 | 优先级 | 栈 | 周期 | 职责 |
| --- | ---: | ---: | ---: | --- | --- |
| `ui` | 1 | 5 | 8192 | 10 ms | `lv_timer_handler()`，所有 LVGL 调用 |
| `gps` | 0 | 1 | 4096 | 5 ms | 读 UART0，喂 UBX 解析器 |
| `radio_test` | 0 | 2 | 8192 | 50 ms 轮询 / 500 ms 发包 | LoRa 自检：只接收，或勾选后周期发 PING |
| `hw_init` | 0 | 3 | 8192 | 一次性 | 电源/SD/传感器/LoRa/音频初始化后自删 |

Arduino `loopTask` 只保活，不承担业务。

**新增任务的上限**：

- Core 0：优先级 ≤ 6（不要抢 LoRa/系统任务）
- Core 1：优先级 ≤ 5（`ui` 是 5，不要饿死界面）

---

## 4. LVGL 与并发

**本项目没有全局 LVGL 锁。** 规则是单任务模型：

- 只有 LVGL 任务可以调用 `lv_*`。
- 其他任务通过以下方式交付数据：
  1. 写队列 / 环形缓冲 / 单写单读快照，由 UI 定时器读取；
  2. `lv_async_call()`（回调仍在 LVGL 任务执行）；
  3. 需要原子性的短结构用临界区保护。
- I2C 走设备 Manager（内部有递归互斥和总线恢复）。
- SD 卡访问加 `SdLockGuard`。

详见 [`concurrency.md`](concurrency.md)。

---

## 5. BSP 分层

```text
应用 / src/dev
      │  只调 BSP API
      ▼
src/bsp/                 板级 façade（引脚、初始化顺序、降级策略）
      │  复用现有驱动
      ▼
src/I2C0  src/I2S  src/gps  src/storage  src/SX1280
      │
      ▼
Arduino / ESP-IDF / TFT_eSPI / LVGL / RadioLib / Adafruit
```

原则：

- `src/bsp/bsp_pins.h` 是硬件参数唯一来源。
- 应用不直接操作 GPIO / I2C / I2S / SD / SX1280。
- BSP 对缺失硬件必须**降级而不是卡死**。例如 QMC5883P 不存在时
  `mag_ok=false`，其余传感器继续工作；SD 挂载失败时返回 `false`，界面照常运行。
- 驱动内部的 Singleton、总线锁、重试逻辑保持原样；BSP 只做薄封装。

---

## 6. 内存

### 6.1 区域

| 区域 | 大小 | 用途 |
| --- | ---: | --- |
| Internal SRAM | 512 KB | 任务栈、DMA、驱动状态 |
| PSRAM (OPI) | 8 MB | LVGL 内存池（512 KB）、绘制缓冲、图片解码、大缓冲 |
| Flash / app0 | ~14.9 MB | 代码与只读数据 |
| DROM | 8 MB | `.rodata`：字符串、常量表、字体位图 |

### 6.2 实测占用

| 固件 | RAM | Flash | DROM |
| --- | ---: | ---: | ---: |
| `guachat_dev`（2026-10-26，含中文字体） | 8.2%（26,772 B） | 11.7%（1,823,789 B） | 21.30%（1,787,024 B） |

dev 基线余量很大，但引入大字体、图片或大常量表时仍要按 [`fonts.md`](fonts.md) 做子集。
DROM 上限约 8 MB，字体与常量表是主要增长点。

### 6.3 分配规则

- LVGL 绘制缓冲：`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`（BSP 已做）。
- 图片/音频大缓冲：优先 PSRAM，成对使用 `heap_caps_free`。
- 不要往 LVGL 内存池里塞大图；池只有 512 KB。
- 大字符串字面量：编译选项已启用 `-mtext-section-literals`，把字面量放到 IROM。
- `-fno-exceptions`：项目不使用 C++ 异常。

---

## 7. 数据流（dev 基线）

```text
MAX17055 ──I2C0──┐
SHT30/BMP581    │
LSM6DS3TRC      ├─ bsp_sensors_read() ─┐
QMC5883P        │                      │
PCF8563T ───────┘                      │
                                       ├─ dev_ui 1 Hz timer（ui_task）
SDCardManager ── bsp_storage_info() ───┤   → lv_label_set_text()
SX1280 ── bsp_radio_available/read() ──┤
GPS UBX ── g_gpsData 快照 ─────────────┘
```

- `g_gpsData` 是单写（gps 任务）单读（UI）结构，依靠字段级写入和 epoch 边界，
  读取时允许拿到“上一帧”的一致快照。
- LoRa 在 UI 里只用非阻塞接口；阻塞 `bsp_radio_receive()` 不得从 UI 任务调用。
- 音频麦克风读取带超时，1 Hz 采样不会长期阻塞界面。

---

## 8. UI 边界

- 新应用一律用**纯 LVGL** 自建界面，`src/dev/dev_ui.cpp` 只是 BSP 用法示例。
- 不要引入外部 UI 生成器输出；界面直接用 LVGL 源码维护。
- 界面应从布局、交互和视觉重新设计，不要只替换 `dev_ui.cpp` 的文字。

---

## 9. 构建产物

```text
.pio/build/guachat_dev/
  bootloader.bin
  partitions.bin
  firmware.bin
```

0x0 合并固件由 `tools/merge_firmware.py` 生成，偏移见
[`build-and-flash.md`](build-and-flash.md)。
