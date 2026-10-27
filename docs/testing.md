# 测试与交付报告

> 编译通过 ≠ 功能正常。本项目把验证分成四层：
> **静态检查 → 主机测试 → 设备测试 → 未验证项**。
> 报告必须分开写。

---

## 0. 完整门禁（交付前）

```bash
python tools/validate.py --all
```

依次执行静态检查、主机测试、构建、合并 `0x0` 镜像，并校验：

- 用 `tools/verify_firmware.py` 独立解析镜像：芯片/Flash、DIO 头、分区表
  magic 与 MD5、app 分区、应用描述符、ELF SHA256；
- 四个组件偏移处的字节与构建目录一致；
- `build/guachat-dev-full.bin` 的大小与 SHA256；
- `build/guachat-dev-full.manifest.json` 中的 merged image、ELF、MAP 身份。

`--firmware` 也会先构建再合并；只有明确要复用现有构建时，才直接调用
`python tools/merge_firmware.py --no-build`。

发布归档单独跑：

```bash
python tools/archive_firmware.py create --env guachat_dev
python tools/archive_firmware.py verify dist/firmware/<sha256>
```

---

## 1. 静态检查

### 1.1 构建

```bash
pio run -e guachat_dev
```

必须 0 error。新增 warning 要处理或说明。

### 1.2 项目检查脚本

```bash
python tools/validate.py --static
```

至少覆盖：

| 检查 | 期望 |
| --- | --- |
| 入口文件、BSP、工具存在 | 通过 |
| `main` 不含应用业务代码 | 通过 |
| 只有一个 `guachat_dev` 环境和一个入口 | 通过 |
| 应用代码出现裸 GPIO / 直接 `Wire` | 警告或失败（按规则） |
| 文档内相对链接 | 不悬空 |
| 中文字符串字体覆盖 | 缺字失败 |
| 射频参数越界 | 警告（默认 2402–2482 MHz、推荐 7 dBm、硬件上限 20 dBm；用户改动自行负责） |
| WiFi / BT / ESP-NOW 符号 | **只警告，不失败**（用户自担合规责任） |
| 密钥/令牌/私钥特征 | 失败（GitHub token、AWS key、PEM 私钥） |
| 冲突标记 `<<<<<<<` / `>>>>>>>` | 失败 |
| GitHub Actions `uses:` 固定 | 必须是 40 位 commit SHA；Docker 镜像必须是 digest |
| 依赖版本固定 | PlatformIO platform 与 `lib_deps` 必须是精确版本，不用 `^` / `~` |

当前基线共 16 项检查，结果以 `validate.py --static` 的汇总行为准。

> 射频与无线的检查是**提示**，不是禁用。用户可以在自己的设备上启用，
> 但报告中必须写明。

### 1.3 合规/危险操作 grep（人工）

```bash
grep -rln "esp_now\|WiFi\.h\|esp_wifi\|WebServer\|HTTPClient\|BLEDevice\.h\|BLE\.h\|esp_bt\|esp_ble" src/ | wc -l
```

有输出不代表失败；确认是否是有意引入，并在报告中说明。

---

## 2. 主机测试

纯逻辑（协议编解码、数学运算、单位换算、状态机）应能在 PC 上测试，不依赖硬件。

```bash
python tools/validate.py --host
# 或
python -m unittest discover -s tools/host_tests -p "test_*.py" -v
```

当前基线共 29 个测试，全部不依赖硬件：

| 文件 | 覆盖 |
| --- | --- |
| `test_repo_contract.py` | 合并偏移、Flash 大小、单环境、射频边界、入口文档 |
| `test_verify_firmware.py` | 合法镜像、组件不一致、分区 MD5、ELF 不匹配、芯片错误、QIO、截断镜像 |
| `test_archive_firmware.py` | 归档创建/校验、幂等、篡改 ELF、多余文件、目录名错误、源 manifest 不一致、受保护根目录 |
| `test_install_skills.py` | `--check` 不写盘、安装/检查、重复安装、`--force` 恢复、目标为文件时拒绝 |

`firmware_fixture.py` 生成合成的 ESP32-S3 镜像、分区表、ELF、boot_app0 与
manifest，供上述工具测试使用。纯逻辑测试用同样的 `unittest` 目录扩展；
没有主机测试时，报告里写 `Host tests: none`。

---

## 3. 设备测试

### 3.1 前提

- 已获得用户烧录授权。
- 串口可用（先开串口再复位，见 [`build-and-flash.md`](build-and-flash.md)）。
- 知道设备当前烧的是哪个环境。
- 确认刷写方式：`full.bin` 从 `0x0` 刷写会写入 NVS 区域，但**不保证清空**；
  本次实测刷完后校准仍保留。需要从零校准/清空设置时先显式擦除 NVS，
  需要保留设置时用 `pio run -e guachat_dev -t upload`。

### 3.2 清单（`guachat_dev` 基线）

> 下表针对仓库自带的六页签硬件自检界面。若已替换成新应用，硬件项仍然适用，
> 显示/触摸项改为验证新界面的布局、点击命中和状态变化，并补测应用自身的功能。

逐项记录“实际现象/读数”，不要只打勾。

| # | 项目 | 操作 | 通过标准 | 实测 |
| --- | --- | --- | --- | --- |
| 1 | 启动 | 上电/复位 | 串口完整打印到 `[BSP] hardware init done`，无重启循环；DRV2605L 正常且强度 > 0 时伴随一次开机震动 | |
| 2 | 显示 | 观察屏幕 | 六个页签正常显示，无花屏/雪花 | |
| 3 | 触摸 | 点击页签/按钮 | 切换、按钮响应正常 | |
| 4 | 电源 | 电源页 | 电压约 3.3–4.4 V，SOC 合理，无 `not detected` | |
| 5 | TF 卡 | 电源页 | 挂载到 `/sd`，容量/剩余合理 | |
| 6 | 温湿度 | 传感页 | SHT30 温湿度合理，露点 ≤ 温度 | |
| 7 | 气压 | 传感页 | BMP581 约 950–1050 hPa（随海拔/天气变化） | |
| 8 | IMU | 传感页 | 静止时 accel 约 (0,0,9.8)，晃动有变化 | |
| 9 | 地磁 | 传感页 | 有磁力计时 `mag_ok`，转动设备航向变化 | |
| 10 | RTC | 传感页 | 时间在走，VL 不报 LOW | |
| 11 | LoRa TX | LoRa 页勾选“持续发送” | 每 500 ms 发一包，TX 计数递增；取消勾选后停止发包 | |
| 12 | LoRa RX | 第二台设备发送 | RX 计数增加，最近数据包更新 | |
| 13 | GPS | GPS 页 | 室外能看到卫星/定位；室内允许无定位 | |
| 14 | 扬声器 | 音频页“蜂鸣” | 能听到提示音，音量滑块有效 | |
| 15 | 麦克风 | 音频页 | 麦克风峰值随环境声音变化 | |
| 16 | 开机震动 | 上电/复位 | DRV2605L 正常、强度 > 0 时震动一次；强度为 0 时不震动 | |
| 17 | 震动强度/效果 | 震动页拖动强度滑块、滚动效果 roller | 松手后按新强度试震；重启后设置保持 | |
| 18 | 触控反馈 | 按钮、音量/背光滑块、开关 | 触控时低压震一下；关闭“触控反馈”后不再震 | |
| 19 | 重新校准 | 震动页“重新校准” | 按钮暂时禁用，1–3 秒马达嗡鸣，完成后显示“校准完成”；失败显示“校准失败” | |
| 20 | LoRa + WiFi 并发 | LoRa 7 dBm，LoRa 频率远离 WiFi 信道 | 两者同时工作；记录丢包率/吞吐；高于 7 dBm 仅串口提示，风险自行评估 | |
| 21 | 实体按键 | 按 BOOT/锁屏、音量加、音量减 | 应用能读取对应按键按下/松开；无粘连、无误触发 | 用户现场确认正常（2026-10-28） |

### 3.3 已知的基线结果（2026-10-27）

本机实测：

- 2026-10-27 从 `0x0` 刷入合并镜像
  `29d5ec6588d8df5e403496a7a88a743613a5c5f12f363f35c76ede93a97f65f2`：
  启动日志显示 `mode:DIO`，走到 `[BSP] hardware init done`，无重启循环。
- 界面点测：显示、触摸、蜂鸣、麦克风峰值、LoRa「持续发送」计数均正常
  （用户现场确认）。
- TCA9535、MAX17055、TF 卡、SHT30（33.3°C / 37.5%RH）、BMP581、PCF8563T、
  LSM6DS3TRC、SX1280 初始化成功。
- **QMC5883P 无应答**（I2C0 扫描不到 0x2C），BSP 已降级；需确认该板硬件。
- **LoRa 只有一台设备**：只验证了初始化与 TX，RX 标为 `Unverified`。
- GPS 室内无定位，属于环境限制。
- **全量镜像不保证清空 NVS**：早先一次从 `0x0` 刷 `full.bin` 后日志出现
  `nvs_open failed: NOT_FOUND`；但 2026-10-27 震动版本同样从 `0x0` 刷入后
  日志显示 `calibration restored from NVS`。合并镜像在 `0x9000` 附近是 0xFF
  填充，写入 0xFF 不保证擦除旧数据。需要从零开始先显式擦除 NVS；
  需要保留设置用 `pio run -e guachat_dev -t upload`。
- BMP581 单位问题已修复（此前实测约 1010 hPa）。
- 自动检查：`tools/validate.py --static` 16 项通过；`tools/validate.py --host`
  29 项通过。
- 独立镜像校验：合并镜像 1,889,696 字节，bootloader 3 段、DIO，分区表 5 项，
  应用 ELF SHA256 匹配；`tools/archive_firmware.py` 归档创建与校验通过。

### 3.4 震动功能版本（2026-10-27，已上机，基本通过）

新增震动页、强度/效果设置、触控反馈与后台重新校准后，门禁与上机结果：

- `python tools/validate.py --all`：静态 16 项、主机 29 项、构建、合并与独立镜像校验全部通过。
- 合并镜像 1,896,112 字节，
  SHA256 `7363379ef72fb34a2a286baa335cb0c3c0e6e82155911b1e5e26f898d8ed03be`；
  ELF SHA256 `c7ba528d4c2070d0abe2df8cb1843abc73b5d14b7bb983f1f429d404a42f548e`。
- 2026-10-27 从 `0x0` 刷入 COM9，写入哈希校验通过。启动日志为
  `mode:DIO`、`[DRV2605L] ready | V=2.51V clamp=3.26V f_res=169Hz | standby`、
  `[BSP] haptic: boot feedback`，走到 `[BSP] hardware init done`，无重启循环。
- 用户现场点测：开机震动、六页签和“震动”页、强度滑块、效果 roller、“试震”、
  触控反馈开关、重新校准及重启后设置保持基本正常
  （定性确认，未记录逐项数值）。
- 本次日志为 `calibration restored from NVS`，全量刷入未清掉已有校准；
  NVS 的注意事项见 3.3。
- QMC5883P 无应答、LoRa RX 单机、GPS 室内无定位仍按 3.3 标为 `Unverified`。

### 3.5 实体按键（2026-10-28，已上机）

新增 `bsp_buttons` 后，用户现场确认三个可编程实体按键读取正常：

- BOOT / 锁屏键（GPIO0）
- 音量加键（TCA9535 P01）
- 音量减键（TCA9535 P00）

定性确认，未记录逐项读数、消抖时间或 I2C0 忙时的丢样率。
开关机键是纯硬件电源锁存，固件不可读，不属于本次测试项。

---

## 4. 报告格式

```text
Build:        PASS
  RAM 8.2% / Flash 11.7% / DROM 21.30%

Host tests:   PASS
  tools/host_tests: 29 passed

Device tests: PASS
  启动：通过，无重启循环
  显示/触摸：通过
  电源：4.05 V / 78% / -120 mA
  TF：挂载 /sd，843 MB 可用
  传感器：SHT30 32.5°C 45.7%RH；BMP581 1010.2 hPa；IMU 正常；RTC 正常
  LoRa TX：返回成功
  音频：蜂鸣有声；麦克风峰值随说话变化
  震动：开机震动一次；强度/效果/触控反馈可用；重新校准完成
  震动持久化：重启后强度、效果、触控反馈保持

Unverified:
  LoRa RX：没有第二台设备
  QMC5883P：该板 I2C0 无应答，需硬件确认
  GPS 定位：室内无信号

Artifacts:
  build/guachat-dev-full.bin
  build/guachat-dev-full.manifest.json
  dist/firmware/<sha256>/（镜像 + ELF + MAP + manifest + archive.json）
  size ...
  sha256 ...
```

### 4.1 写作规则

- 状态只写 `PASS / FAIL / NOT RUN`；没跑就是 `NOT RUN`，不能写“应该没问题”。
- 编译通过只能写 `Build: PASS`，不能升级成 `Device tests: PASS`。
- 没有实测的项，不要写“正常”。
- 失败项要写现象、日志关键行、初步判断。
- 改动过射频/分区/启动配置时，单列“风险”段落。
- 报告是给用户看的，不是给脚本看的；避免只贴日志。

---

## 5. 什么时候可以判定“完成”

- [ ] `python tools/validate.py --all` 通过（静态 + 主机 + 构建 + 合并 + 产物校验）
- [ ] 静态检查通过（警告需解释）
- [ ] 设备测试清单中与需求相关的项全部实测
- [ ] 未验证项明确列出原因
- [ ] `build/guachat-dev-full.manifest.json` 已生成，merged image 与 ELF 身份匹配
- [ ] 合并固件已通过 `verify_firmware.py` 独立校验
- [ ] 需要归档时，`archive_firmware.py verify dist/firmware/<sha256>` 通过
- [ ] 未混入应用业务代码或第二个 `setup()`
- [ ] 未擅自烧录（烧录已获授权）
- [ ] 推送到 GitHub 时 CI 通过；Gitee 镜像上已在本地跑过同样门禁
