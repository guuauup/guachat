# 排障

> 按现象查。先看串口日志，再改代码。
> 启动日志采集方法见 [`build-and-flash.md`](build-and-flash.md#52-抓启动日志重要)。

---

## 1. 启动

### 1.1 反复重启 / `TG0WDT_SYS_RST`

| 可能原因 | 判断 | 处理 |
| --- | --- | --- |
| 合并固件强制 QIO | ROM 日志显示 `mode:QIO`，启动循环 | 去掉 `--flash_mode qio`，保留 DIO 头 |
| 看门狗超时 | 日志停在某个初始化 | 检查是否有任务忙等；临时放宽 WDT |
| 电源不足 | 接 USB 也重启，电压跌落 | 换线/供电，测电池电压 |
| Flash 参数不匹配 | 16 MB 板子刷了错误分区 | 确认 `partitions_16MB_app.csv` |

### 1.2 无串口输出

- 固件使用 `Serial.setTxTimeoutMs(0)`：**必须先打开串口再复位**。
- 确认波特率 115200。
- 确认 COM 口是 ESP32-S3 原生 USB（VID 303A）还是串口芯片。
- 若刚刚上传，等待 1–2 秒再打开端口。

### 1.3 黑屏但串口正常

- 背光在 `bsp_display_init()` 里先设为 3%，`dev_ui_build()` 再设为默认
  100（约 30%）；确认执行到了。
- 检查 TCA9535 是否初始化成功（P16 显示屏复位）。

### 1.4 震动异常（无电源指示灯）

| 可能原因 | 判断 | 处理 |
| --- | --- | --- |
| DRV2605L 无应答 | 串口有 `[DRV2605L] ERROR: device not found`；I2C0 扫描无 0x5A | 查焊接/供电/上拉；EN 已上拉到 VDD |
| 强度设置为 0 | “震动”页强度显示“关闭”；NVS `drv2605/alert_lvl=0` | 调到 1–10；**0 会同时关闭开机震动**（与原机一致） |
| 触控反馈没有震动 | “震动”页触控反馈关闭；NVS `drv2605/kb_on=false` | 打开开关；按钮/滑块用 `bsp_haptic_key_click()` |
| 首次校准未完成或失败 | 日志有 `LRA auto-calibration... timeout` | 确认马达连接；仍会用 `BSP_HAPTIC_LRA_PERIOD` 尝试播放 |
| 重新校准失败 | “震动”页显示“校准失败”；日志 `WARN`/`timeout` | 确认马达连接；失败后仍使用固定 LRA 周期，可再试 |
| 震动太弱 | 能感觉到但很轻 | 调“震动”页强度；硬件默认值见 `BSP_HAPTIC_ALERT_RV`（约 21.6 mV/级） |
| 固件删掉了开机反馈 | `src/main.cpp` 无 `bsp_haptic_boot_feedback()` | 恢复调用；见 `AGENTS.md` 2.6 |

- 首次启动（NVS 无校准）或显式擦除 NVS 后，会先自动校准 1–2 秒再播放开机震动。
  `full.bin` 从 `0x0` 刷写不保证清空 NVS；需要从零校准时先显式擦除。
- 播放后由 `loop()` 的 `bsp_haptic_tick()` 收回 standby；不要删除它。
- “重新校准”在 Core 0 的 `haptic_cal` 任务里阻塞 1–3 秒；校准期间按钮/滑块反馈会
  暂时跳过，属于正常现象。
- LVGL 缓冲分配失败会停在 `while(true)`；看日志 `LVGL buffer allocation failed`。

---

## 2. 显示

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 雪花/条纹 | LoRa 与 TFT 共用 SPI2 | LoRa 必须用 SPI3 |
| 颜色反转/偏色 | ST7796 初始化参数 | 检查 `my_user_setup.h` 与 `bsp_display.cpp` |
| 局部花屏 | 绘制缓冲/刷新区域错误 | 检查 `BSP_LV_BUF_LINES` 与 flush |
| 亮度太低 | 背光安全上限 170/2047 | 这是硬件保护，不要绕过；确认 `bsp_backlight_set` |
| 中文方块 | 字库缺字 | 见 [`fonts.md`](fonts.md) |

---

## 3. 触摸

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 完全无触摸 | FT6336U 未初始化 / RST 未释放 | 看日志 `[BSP] touch`；确认 TCA9535 P15 |
| 坐标偏移 | 旋转/校准 | 检查 `bsp_input.cpp` 的坐标映射 |
| 偶发断触 | I2C1 干扰 / INT 配置 | 检查 47/48 上拉与走线 |

---

## 4. I2C

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 某个设备 `NOT FOUND` | 未焊接/供电/地址错 | 先跑 `bsp_i2c_scan()` 看地址 |
| 全部 I2C 设备消失 | 总线被拉死 | 断电重启；使用 `i2cBusLock` 的总线恢复 |
| `i2cWriteReadNonStop returned Error -1` | 设备无应答 | 通常伴随 `NOT FOUND`，先查硬件 |
| `nvs_open failed: NOT_FOUND` | 没有校准命名空间 | 无害；首次运行会出现，可忽略 |

**QMC5883P 特别注意**：如果扫描不到 0x2C，不要先改驱动。
确认模块是否焊接、供电、I2C 上拉；BSP 会在缺失时降级为 `mag_ok=false`。

---

## 5. TF 卡

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 挂载失败 | 卡格式/接触/供电 | 格式化为 FAT32；重插；换卡 |
| 偶发崩溃 | 跨任务未加锁 | 所有访问加 `SdLockGuard` |
| 容量不对 | 分区表/卡类型 | 用 `bsp_storage_info()` 看实际值 |
| 热插拔后异常 | 未卸载 | 先 `bsp_storage_unmount()` |

---

## 6. 传感器

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| BMP581 显示 10.2 hPa | 单位换算错误 | Adafruit 库是 hPa，BSP 乘 100 转 Pa |
| 气压不变化 | ODR 1 Hz，正常波动很小 | 等待/改变高度；不是故障 |
| 航向不动 | 无磁力计/未校准 | 看 `mag_ok`；确认 QMC5883P |
| 加速度方向反 | 安装方向 | 在应用层做坐标变换，不要改驱动 |
| RTC 时间不对 | 电池/首次上电 | 设置时间；检查 VL 标志 |

---

## 7. LoRa

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 初始化失败 | SPI3/电源/ BUSY | 看日志；确认 3.3 V 与天线 |
| TX 成功但对端收不到 | 频率/SF/BW/前导码不一致 | 两端参数必须一致 |
| RX 计数不增加 | 没重新武装 RX | 读包/发完后 `bsp_radio_start_rx()` |
| 距离很近也丢包 | 天线/功率/遮挡 | 检查天线连接；默认 7 dBm 本身距离有限 |
| 屏幕出现条纹 | LoRa 改回 SPI2 | 必须用 SPI3 |

没有第二台设备时，RX 无法验证，报告中写 `Unverified`。

---

## 8. 音频

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 无声 | 功放未使能 / SD_MODE | 看 `[MAX98357]` 日志 |
| 声音失真 | 采样率/位宽不匹配 | 位宽/声道保持 32-bit 立体声；采样率与 `bsp_audio_set_sample_rate()` 一致（默认 8 kHz） |
| 麦克风无数据 | ZTS6672 未就绪 | 看 `[ZTS6672]` 日志；检查 I2S RX |
| 长音卡界面 | 在 UI 回调里阻塞播放 | 移到后台任务 |

---

## 9. 构建

| 错误 | 处理 |
| --- | --- |
| `multiple definition of setup()` | `src/` 下有两个入口；只保留 `src/main.cpp` |
| `drom0_0_seg overflow` | 字体/常量表太大；子集化或移 PSRAM |
| `undefined reference to ...` | 源文件不在 `src/` 下，或被自定义 `build_src_filter` 排除 |
| `Could not open COM9` | 端口占用/复位中；关监视器重试 |
| Linux 下头文件找不到 | 路径大小写不一致；统一小写 |

---

## 10. 获取帮助前准备

1. 完整启动日志（从 `rst:` 到 `[BSP] hardware init done`）。
2. `pio run -e guachat_dev` 的尾部 30 行。
3. 你改过的文件列表（`git status` / `git diff --stat`）。
4. 设备型号、Flash/PSRAM 配置、外设接线。
5. 已经排除了什么（例如“换过卡”“测过电压”）。
