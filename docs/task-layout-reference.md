# 完整产品任务分配参考（消费者固件，非基线契约）

> **这是一份参考案例，不是 `guachat_dev` 的硬性规则。**
>
> 数据以 `guachat-2g4` 消费者固件为准（2026-10-28 查看）。
> 该版本已经移除 WiFi、BLE、Web 控制台、ESP-NOW 和网络服务任务，
> 只保留 LoRa 传输层；Core 0 基本只跑 `radio`。
>
> 新应用任务更少时，不要为了“对齐”而创建这里出现的所有任务。
> 需要突破当前基线优先级上限时，必须有实测依据和真机验证。

---

## 1. 与当前开发基线的区别

当前公开仓库的 `guachat_dev` 只保留最小任务集：

| 任务 | 核心 | 优先级 | 栈（字节） | 周期 | 职责 |
| --- | ---: | ---: | ---: | --- | --- |
| `ui` | 1 | 5 | 8192 | 10 ms | LVGL 渲染、触摸、所有 `lv_*` |
| `gps` | 0 | 1 | 4096 | 5 ms | UART0 + UBX 解析 |
| `radio_test` | 0 | 2 | 8192 | 50 ms / 500 ms | LoRa 自检收发 |
| `haptic_cal` | 0 | 2 | 6144 | 按需 | 后台重新校准，完成后自删 |
| `hw_init` | 0 | 3 | 8192 | 一次性 | 外设初始化，完成后自删 |
| Arduino `loopTask` | 1 | 1 | 20480 | 100 ms | 只做 `bsp_haptic_tick()` 等低频维护 |

基线规则：

- Core 0 新任务优先级 **≤ 6**
- Core 1 新任务优先级 **≤ 5**（`ui` 是 5，不要饿死界面）

消费者固件里 Core 1 的 `voice` 用了优先级 6，属于
**实时音频场景的例外**。默认开发仍应遵守上面的上限。

---

## 2. 核心分工

```text
Core 0：LoRa 射频 + IDF 系统任务
  radio（SX1280 / CSMA / 收发队列）
  esp_timer、IPC 等

Core 1：UI + 传感器 + 音频 + 存储
  voice、ui、audio、sensor、gps、vario、sd、i2cSlow、weather
```

这个划分的目的：

- UI 和 LVGL 固定在 Core 1，避免被 Core 0 的射频/系统任务干扰
- 音频链路和 UI 同核，但用更高优先级保证 I2S 喂数据
- 慢速后台任务集中在一个核，靠阻塞点让出 CPU
- Core 0 留给 LoRa；消费者固件没有 WiFi/BLE，所以不存在 WiFi 抢核问题

> 如果以后做带 Web 控制台/文字 API 的网络/Web 版本，Core 0 会多出 `web`、
> WiFi/lwIP 任务，射频时延要重新评估。见 3.1。

---

## 3. Core 0 任务

消费者固件实际只有：

| 任务 | 优先级 | 栈（字节） | 行为 | 说明 |
| --- | ---: | ---: | --- | --- |
| `radio` | 6 | 7168 | LoRa 收发/CSMA 主循环，队列轮询 + `vTaskDelay(1)` | 收包时阻塞在 RadioLib |
| IDF 系统任务 | 18–25 | 1–4 KB | `esp_timer`、IPC 等 | 均高于应用任务；具体值随配置 |
| `idle0` | 0 | 1024 | 空闲 + watchdog | 框架创建 |

实际优先级顺序：

```text
IDF 系统任务(18–25) > radio(6) > idle(0)
```

其中 `esp_timer` 通常 22、IPC 24 左右；没有 WiFi 时不会创建 WiFi 任务。
不同 IDF 版本和配置会有差异，以实际 `sdkconfig` 为准。

### 3.1 网络/Web 版本才会出现的任务

下面这些**不在当前消费者固件里**，来自之前的完整网络/Web 版分配，
不能当成 `guachat-2g4` 的现状：

| 任务 | 核心 | 优先级 | 栈（字节） | 说明 |
| --- | ---: | ---: | ---: | --- |
| `web` | 0 | 5 | 10240 | Web 控制台 + 文字 API 对话，不做模型推理 |
| `espnow` | 0 | 6 | 8192 | 仅 ESP-NOW 传输版本创建，和 `radio` 同优先级 |
| WiFi | 0 | 23 | — | 固定 Core 0，会抢占 `radio` |
| lwIP | 0 | 18 左右 | — | 网络栈任务 |

> 旧代码里这个任务的名字容易让人误以为它做模型推理，但它只负责
> Web 页面和文字 API 转发，不做推理。本文统一按职责改称 `web`。

如果启用网络/Web 版：

- WiFi 任务优先级 23，会直接压住 `radio`(6)，LoRa 时延会变差
- `web` 只处理 Web 页面和文字 API，10 KB 栈对 HTTP/JSON 的压力较小；
  是否够用要看实际请求解析，走 HTTPS/TLS 时更必须看真实水位
- `espnow` 与 `radio` 同优先级，双开时靠时间片轮转，属于降级方案
- 不要在没有实测的情况下把网络/Web 版的任务表套到消费者固件上

---

## 4. Core 1 任务

| 任务 | 优先级 | 栈（字节） | 周期 / 阻塞方式 | 职责 |
| --- | ---: | ---: | --- | --- |
| `voice` | 6 | 24576 | 队列 / 5 ms 轮询 | Codec2 编解码、录音、播放 |
| `ui` | 5 | 18432 | 20 ms `vTaskDelayUntil` | LVGL 渲染、触摸、UI 刷新 |
| `audio` | 4 | 3072 | `xTaskNotifyWait` 永久阻塞 | 提示音 WAV 播放，独占 I2S |
| `sensor` | 3 | 4096 | 5 ms `vTaskDelayUntil`，4 相轮转 | IMU、气压、SHT30、电量 |
| `gps` | 1（飞行时 3） | 2048 | `vTaskDelay(1)` 轮询串口 | UBX 字节喂状态机 |
| `vario` | 1（飞行时 3） | 2560 | 持续喂 I2S，阻塞在 `i2s_write`/互斥锁 | 变音音频 feeder |
| `sd` | 1 | 4096 | 通知唤醒 / 60 s 超时 | 插拔挂载、测速、U 盘模式 |
| `i2cSlow` | 1 | 3072 | 通知唤醒 / 1 s 超时 | 马达 tick、RTC 读取、TCA9535 ISR |
| `weather` | 1 | 4096 | 60 s `vTaskDelayUntil` | 气压趋势、天气预报 |
| `loopTask` | 1 | 20480 | `setup()` 跑完即 `vTaskDelete` | Arduino 启动壳，退出后释放 |
| `idle1` | 0 | 1024 | 空闲 + watchdog | 框架创建 |

Core 1 的优先级顺序：

```text
voice(6) > ui(5) > audio(4) > sensor(3) > 后台任务(1) > idle(0)
```

关键理由：

- `voice` 必须高于 `ui`，否则滑动界面会抢走 I2S 喂数据，导致 DMA 欠载
- `audio` 是 4，高于变音 feeder（飞行时 3），提示音能及时拿到 I2S 锁
- `ui` 高于所有慢速后台任务
- 飞行模式下 `gps` / `vario` 从 1 提到 3，提高实时性，但仍低于 `ui`
- `vario` 实际是 **2560 B**，不是 2048 B；代码里明确注释了从 2048 调大

---

## 5. 栈与堆

### 5.1 栈单位

ESP-IDF 的 `xTaskCreate*` 栈大小单位是**字节**，不是 word。
Arduino-ESP32 下同样是字节。

### 5.2 栈总量

```text
Core 0：radio 7 KB + IDF 系统任务
Core 1：2 + 18 + 3 + 4 + 2 + 2.5 + 4 + 3 + 4 + 24 ≈ 66.5 KB
启动阶段还要加 loopTask 的 20 KB，setup 结束后释放
```

### 5.3 为什么任务栈放内部 SRAM

- 本项目创建任务时没有显式指定 PSRAM，任务栈按当前构建落在内部 SRAM
- PSRAM 只用于 LVGL 池、音频缓冲、语音重组池等大块数据
- NVS 写入时会短暂关闭 cache，PSRAM 栈在这个窗口不可访问，风险很高
- TLS 等需要确定可访问性的栈也必须放内部 RAM

注意：`sdkconfig.defaults` 里即使写了
`CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y`，也不要直接假定它在
PlatformIO 的 Arduino 预编译框架里生效；以实际运行和构建配置为准。

### 5.4 实测依据

- `voice` 实测峰值 17936 B，分配 24576 B，留约 27% 余量
- 其他任务多按调用链和实测水位给栈
- 用 `uxTaskGetStackHighWaterMark()` 监控；IDF 下返回值单位是字节

### 5.5 建议

- 栈大小只写一处，创建任务和 watermark 表共用同一组 `constexpr` 常量
- 注释里的旧数值要及时清理；该产品里就出现过 6 KB/7168、3 KB/2048、
  2 KB/3072、28 KB/24576 这类注释漂移
- 新任务先给保守值，跑满负载后看 high-water mark，再收紧

---

## 6. 启动时序

消费者固件的实际顺序（按 `main.cpp` 整理）：

```text
setup() 在 loopTask(Core 1, prio 1, 20 KB) 上运行
 ├─ 早期：MAX98357 初始化 → ToneManager::earlyInit()
 │        → 创建 audio(Core 1, prio 4, 3072)
 ├─ 显示/触摸/LVGL 初始化
 ├─ 创建 gps(Core 1, prio 1, 2048)
 ├─ varioInit() → 创建 vario(Core 1, prio 1, 2560)
 ├─ 创建 ui(Core 1, prio 5, 18432)
 ├─ 创建 sd(Core 1, prio 1, 4096)
 ├─ 创建 i2cSlow(Core 1, prio 1, 3072)
 ├─ 创建 sensor(Core 1, prio 3, 4096)
 ├─ 创建 weather(Core 1, prio 1, 4096)
 ├─ setFlightMode()：飞行时把 gps/vario 提到 3
 ├─ lora::radioInit() → 创建 radio(Core 0, prio 6, 7168)
 ├─ transport::init()（消费者固件为 no-op）
 ├─ VoiceManager::init() → 创建 voice(Core 1, prio 6, 24576)
 └─ vTaskDelete(nullptr)：删除 loopTask，回收 20 KB
```

要点：

- `setup()` 结束前不要返回，否则 Arduino 会进入 `loop()`
- 删除 `loopTask` 后，所有业务都必须在显式创建的任务里运行
- 早期音频任务先创建，保证开机提示音可用
- `voice` 放到最后创建，是为了在 WiFi/ESP-NOW 尚未拉起时拿到
  24 KB 连续内部 SRAM；消费者固件虽然没有 WiFi，这个顺序仍然保留

---

## 7. 已知风险与边界

### 7.1 GPS：已做缓冲缓解，但仍是 UI 长卡顿的第一观察点

实际代码不是“裸奔”的 256 B 缓冲：

- `Serial0.setRxBufferSize(2048)`，230400 bps
- `gps` 任务 2048 B 栈，优先级 1，飞行时提到 3
- `vTaskDelay(1)` 轮询，不阻塞在 UI
- 代码注释明确记录：默认 256 B 时，拖动地图会让 5 Hz 掉到 3/2 Hz

按 20 Hz、每 epoch 约 650 B 估算，2 KB 缓冲大约能扛 **150 ms**
的 UI 连续抢占；超过这个窗口仍可能丢字节。地图拖动、整屏重绘这类
长渲染是主要压力来源。

建议：

- 用实际地图拖动/UI 满负载测试丢 epoch 率，不要只看静态余量
- 如果仍丢包，优先把 RX 缓冲加到 4 KB，或改成 UART 事件/环形缓冲
- `gps` 任务栈只有 2048 B；不要在任务里加浮点格式化、大量日志、
  大局部数组，先用 high-water mark 看余量
- 必要时提高 `gps` 优先级，或在 UI 里拆分长渲染

### 7.2 Vario：优先级低，但有 256 ms DMA 缓冲兜底

实际代码：

- `vario` 任务 2560 B 栈，非飞行优先级 1，飞行 3
- I2S DMA 配置为 8 × 256 帧，约 2048 帧，8 kHz 下约 **256 ms**
- `writeTone()` 每次喂 64 帧（约 8 ms），DMA 满时阻塞让出 CPU
- 非飞行时也持续喂静音，避免 DMA 欠载产生“断电噗声”

所以它不是“没有保护的 prio 1 任务”，而是用 256 ms 缓冲换优先级。
剩余风险是：`ui`（prio 5）单次渲染或临界区超过 256 ms，DMA 仍会欠载。
飞行时 `vario` 只提到 3，仍低于 `ui`，缓冲才是主要保障。

建议：

- 实测最坏 UI 渲染时间；如果接近或超过 200 ms，考虑提高 feeder 优先级、
  加大 DMA 缓冲，或把音频 feeder 拆到独立核
- 不要在 `varioTask` 里加阻塞日志、SD 访问或大计算
- 语音/提示音占用 I2S 锁时，确认它们自己持续喂 DMA；否则 feeder 暂停
  期间仍可能欠载

### 7.3 Web/网络栈：只在网络/Web 版存在，10 KB 主要面向 Web/文字 API

当前消费者固件没有 `web` 任务。之前的网络/Web 版里，`web` 栈 10240 B，
主要运行 Web 控制台和文字 API 对话，不承担语音编解码或模型推理；
语音在独立的 `voice` 任务（24576 B）。

因此：

- 如果 Web 控制台/API 走普通 HTTP + JSON，10 KB 压力相对小；
  是否够用仍要看实际请求解析和缓冲
- 如果走 HTTPS/TLS，握手、证书链、加密缓冲会明显吃栈，
  10 KB 可能偏紧；以真实 endpoint 的 high-water mark 为准
- 不要按模型推理栈估算；这个任务只是网络/文本层
- 网络/Web 版才需要这条；消费者固件可以忽略

### 7.4 同优先级后台任务的时间片抖动

`gps` / `vario` / `sd` / `i2cSlow` / `weather` 都是 1 级，靠 1 ms tick
轮转。任何任务如果持锁太久或忘记阻塞，都会拖住同级的其他任务。

要求：

- 所有长循环必须有 `vTaskDelay` / `vTaskDelayUntil` / 队列阻塞
- I2C、SD 的互斥区尽量短
- 不要在低优先级任务里做长时间解码或大块内存拷贝

### 7.5 WiFi 抢占只适用于网络/Web 版

消费者固件不启用 WiFi/BLE，Core 0 没有 WiFi 任务，`radio` 不会被
优先级 23 的系统任务压住。

如果以后启用网络/Web 版：

- WiFi 任务优先级 23、固定 Core 0，会直接压住 `radio`(6)
- ESP-NOW 与 LoRa 双开时，只能靠队列、重传和降级策略兜底
- LoRa 时延敏感的场景应避免同时开 WiFi

### 7.6 `sdkconfig.defaults` / `build_flags` 的陷阱

在 PlatformIO + Arduino 预编译框架下：

- `sdkconfig.defaults` 不一定会成为最终 IDF 配置
- `build_flags` 里的 `-DCONFIG_*` 主要影响本项目源码，
  不保证改变预编译 IDF 库的行为
- 该产品里同时存在 `sdkconfig.defaults` 的 WDT=10 s 和
  `build_flags` 的 `-DCONFIG_TASK_WDT_TIMEOUT_S=15`，这类冲突要以
  实际运行结果为准

不要只看配置文件就假定 tick 率、watchdog、系统任务优先级已经生效。

### 7.7 `loopTask` 被删除

消费者固件把 `ARDUINO_LOOP_STACK_SIZE` 设为 20480，`setup()` 末尾
`vTaskDelete(nullptr)`。删除后 `loop()` 不会运行，任何依赖 Arduino
`loop()` 的库或代码都要改成显式任务。

---

## 8. 新应用如何借鉴

不要把这张表直接复制到新项目。按需要裁剪：

1. **先只创建必需任务**
   最小应用通常只需要 `ui` + 一个传感器/后台任务 + 一个射频任务。

2. **优先级从保守值开始**
   - Core 0：`radio` 6，其他 ≤ 5
   - Core 1：`ui` 5，其他 ≤ 4
   只有实时音频等明确需求才考虑高于 UI。

3. **先测量，再调优先级**
   用 high-water mark 看栈，用实际负载看丢包、I2S 欠载、UI 帧率。

4. **遵守 LVGL 单任务模型**
   只有 `ui` 任务可以调用 `lv_*`；其他任务用队列、快照或
   `lv_async_call()`。

5. **所有阻塞接口都不要放 UI 回调**
   I2C、SD、I2S、LoRa 的阻塞操作放后台任务。

6. **给每个任务写清楚阻塞点**
   如果一个任务没有队列、通知、延时或 I2S 阻塞，它就不该长期存在。

7. **不要把“消费者固件”和“网络/Web 版”混在一起**
   当前 `guachat-2g4` 没有 WiFi/BLE/Web 服务/ESP-NOW 任务；
   要加网络功能，按 3.1 重新做时延和内存评估。

---

## 9. 移植到 ESP-IDF 时

FreeRTOS 语义基本一致，这张表可以作为任务划分参考，但要注意：

- IDF 没有 Arduino `loopTask`，需要自己创建 `app_main` 或主任务
- IDF 的 `xTaskCreate` 栈单位仍是字节
- IDF 下 `sdkconfig` 是真正生效的，可以正确设置 tick、watchdog、
  系统任务优先级
- IDF 5.x 的 I2S/I2C 驱动与 4.4 不同，任务优先级可以沿用，
  但阻塞方式和栈需求要重新测量
- 消费者固件没有 WiFi/BLE，Core 0 只有 `radio`，移植时不要把网络/Web 版
  的任务表一起搬过去
- 任务栈仍然建议放内部 SRAM；PSRAM 只放大缓冲

---

## 10. 结论

这套分配的核心思想是成立的：

- 射频和 UI 分核
- 实时音频高于 UI
- UI 高于慢速后台
- 栈按实测水位给
- 大缓冲放 PSRAM，任务栈放内部 SRAM
- 用队列、通知、延时替代忙等
- GPS 用 2 KB 串口缓冲、vario 用 256 ms DMA 缓冲，分别兜住 UI 抢占

它不是可以直接复制的模板，但作为“完整产品如何安排任务”的参考，
对 Agent 很有价值。使用时记住一句话：

> **优先级是需求的结果，不是模板的复制品。**
