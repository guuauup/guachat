# 并发与 LVGL 规则

> 本项目**没有全局 LVGL 锁**。正确性来自“只有 LVGL 任务碰 LVGL”这一条硬规则，
> 加上 I2C/SD 的总线级互斥。

---

## 1. 单任务 LVGL 模型

### 1.1 谁可以调用 `lv_*`

| 环境 | LVGL 任务 | 说明 |
| --- | --- | --- |
| `guachat_dev` | `ui_task`（Core 1，prio 5，10 ms） | `dev_ui.cpp` 的定时器与事件回调都在这里 |

只有这个任务可以：

- 创建/删除控件
- 修改文字、颜色、位置
- 创建/删除定时器、动画
- 调用 `lv_timer_handler()`

### 1.2 其他任务怎么更新界面

| 方式 | 用法 | 适用 |
| --- | --- | --- |
| 队列 | 后台 `xQueueSend`，UI 定时器 `xQueueReceive` | 事件、消息、采样流 |
| 单写单读快照 | 后台写全局结构，UI 读；字段级写入 | GPS、状态标志 |
| `lv_async_call()` | 后台调度，回调在 LVGL 任务执行 | 少量、一次性更新 |
| 临界区 | `portENTER_CRITICAL` 保护短结构 | 多字段一致性 |

**反例**：

```cpp
// 错误：radio 任务直接改控件
void radio_task(void *) {
    lv_label_set_text(label, "rx");   // 可能死机/断言
}
```

**正例**：

```cpp
// radio 任务
xQueueSend(s_ui_queue, &msg, 0);

// ui_timer（LVGL 任务）
Msg msg;
if (xQueueReceive(s_ui_queue, &msg, 0) == pdTRUE) {
    lv_label_set_text(s_label, msg.text);
}
```

---

## 2. 任务优先级

| 核心 | 已有任务 | 新任务上限 |
| --- | ---: | ---: |
| Core 0 | `hw_init` = 3，`gps` = 1 | **≤ 6** |
| Core 1 | `ui` = 5 | **≤ 5**（不要饿死界面） |

- 新任务不要用 `tskIDLE_PRIORITY` 以下的数值（无意义）。
- 长循环必须 `vTaskDelay` / `vTaskDelayUntil`，不能忙等。
- 栈按用途给：纯逻辑 4 KB，带 LVGL/文件/浮点 8 KB 起。

---

## 3. I2C

- 所有 I2C0 访问必须经过设备 Manager；Manager 内部使用
  `i2cBusLock()`（递归互斥）和总线恢复。
- **不要**直接 `Wire.beginTransmission()` / `Wire.requestFrom()`。
- BSP 的 `bsp_power_read()`、`bsp_sensors_read()` 已经持锁，可从任意任务调用。
- 一次 `bsp_sensors_read()` 包含多次事务（SHT30 测量、BMP581、IMU、RTC），
  约几 ms 到十几 ms；1 Hz 足够，不要每帧调用。
- 触摸使用独立 I2C1（47/48），不与传感器总线冲突。

---

## 4. SD 卡

- 跨任务访问必须加 `SdLockGuard`：

```cpp
#include "storage/SDCardManager.h"

{
    SdLockGuard lock;
    // fopen / fread / fwrite / fclose
}
```

- 不要在持锁期间做长时间解码或 UI 操作。
- `bsp_storage_init()` 会重试挂载，放在后台任务，不要放 UI 回调。
- 热插拔前 `bsp_storage_unmount()`。

---

## 5. 音频

- `bsp_audio_write_pcm16()` / `bsp_audio_beep()` 会阻塞；不要在 UI 回调里播长音。
- 麦克风读取带超时；`bsp_audio_read_pcm16()` 的 `timeout_ms` 要显式给出。
- 播放与录音共享 I2S0，驱动内部有互斥；不要绕过 BSP 直接操作 I2S。
- `bsp_audio_set_sample_rate()` 会重配整条 I2S 时钟，必须在没有播放/录音时调用。

---

## 6. 射频

- `bsp_radio_receive()` 和 `bsp_radio_send()` 都是阻塞接口。
- UI 只使用：

```cpp
if (bsp_radio_available()) {
    size_t len = 0;
    uint8_t buf[64];
    if (bsp_radio_read(buf, sizeof(buf), &len) == 0) { /* ... */ }
    bsp_radio_start_rx();
}
```

- 读完包或发送后都要重新 `bsp_radio_start_rx()`。
- LoRa 使用 SPI3，不得改回 SPI2（会与屏幕冲突，出现雪花/条纹）。

---

## 7. 日志

- **`uiTask` 循环里禁止周期性 `Serial` 输出**。Windows 主机 CDC 轮询可能让
  `Serial.write` 阻塞数百毫秒，界面拖动会卡死。
- 启动阶段允许打印少量检查点。
- 需要持续日志时，放到独立低优先级任务，或降低到几秒一次。

---

## 8. 退出与清理

应用/页面退出时必须：

- `lv_timer_del()` 删除自己创建的定时器
- `lv_anim_del()` 停止动画
- 停止并删除自己创建的任务（`vTaskDelete`）
- 释放 PSRAM/堆缓冲（`heap_caps_free` / `free`）
- 注销自己注册的回调

否则切换页面后会出现“幽灵回调”，访问已删除的控件。

---

## 9. 检查清单

- [ ] 所有 `lv_*` 只在 `ui_task` / LVGL 定时器 / LVGL 事件回调里
- [ ] 后台任务没有直接改控件
- [ ] 新任务优先级：Core 0 ≤ 6，Core 1 ≤ 5
- [ ] I2C 走 Manager，SD 走 `SdLockGuard`
- [ ] `uiTask` 没有周期 Serial
- [ ] 读完/发完 LoRa 包后重新 `bsp_radio_start_rx()`
- [ ] 退出时删除了定时器/动画/任务/缓冲
