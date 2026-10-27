# 构建、烧录与恢复

> 目标：从一台干净电脑出发，能构建 `guachat_dev`，生成可从 `0x0` 刷写的合并固件，
> 并在出错时恢复。

---

## 1. 环境

| 依赖 | 版本 / 说明 |
| --- | --- |
| Python | 3.8+ |
| PlatformIO Core | 6.x（命令行 `pio`，或 VS Code PlatformIO 插件） |
| Git | 任意较新版本 |
| USB | ESP32-S3 原生 USB（Windows 10+ 免驱）；若板子走 CH340/CP210x，需要对应驱动 |

安装 PlatformIO：

```bash
python -m pip install -U platformio
pio --version
```

首次构建会自动下载：

- `platform-espressif32 @ 6.5.0`
- `framework-arduinoespressif32`
- `tool-esptoolpy`
- `lib_deps` 里的 Adafruit / RadioLib / TCA9555 / TFT_eSPI 等

> `lib/lvgl` 是**仓库内固化的定制版 LVGL 8.3**。不要执行
> `pio pkg update` 或从注册表重新拉取 LVGL，否则会覆盖定制修改。

---

## 2. 构建

### 2.1 开发基线（默认）

```bash
pio run -e guachat_dev
```

产物：

```text
.pio/build/guachat_dev/bootloader.bin
.pio/build/guachat_dev/partitions.bin
.pio/build/guachat_dev/firmware.bin
.pio/build/guachat_dev/firmware.elf
```

### 2.2 清理

```bash
pio run -e guachat_dev -t clean
# 或只删构建目录
rm -rf .pio/build/guachat_dev
```

不要删 `lib/lvgl` 或 `.pio/libdeps` 之外的用户源码。

### 2.3 资源预算

构建结束会打印：

```text
RAM:   [=         ]   8.2% (used 26772 bytes from 327680 bytes)
Flash: [=         ]  11.7% (used 1823789 bytes from 15597568 bytes)
```

以及链接器分段：

```text
drom0_0_seg: 1787024 B / 8388576 B (21.30%)
```

关注点：

- `drom0_0_seg` 是最容易溢出的区域（字体、字符串、常量表）。
- dev 环境预算充足；DROM 上限约 8 MB，字体与常量表是主要增长点。
- 出现 `drom0_0_seg overflow` 时，先做字库子集或把常量表移到 PSRAM/IROM。

---

## 3. 合并 0x0 固件

ESP32-S3 从 `0x0` 开始读 bootloader。把四个文件按偏移合并：

| 偏移 | 文件 | 来源 |
| --- | --- | --- |
| `0x0` | `bootloader.bin` | `.pio/build/<env>/bootloader.bin` |
| `0x8000` | `partitions.bin` | `.pio/build/<env>/partitions.bin` |
| `0xe000` | `boot_app0.bin` | `~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin` |
| `0x10000` | `firmware.bin` | `.pio/build/<env>/firmware.bin` |

### 3.1 用工具（推荐）

```bash
python tools/merge_firmware.py                # 默认 guachat_dev，合并后自动校验
python tools/merge_firmware.py --zip          # 同时生成恢复包
python tools/merge_firmware.py --no-verify    # 仅在明确知道原因时跳过校验
```

产物：

```text
build/guachat-dev-full.bin
build/guachat-dev-full.bin.sha256
build/guachat-dev-full.manifest.json
build/guachat-dev-full-restore.zip            # 加 --zip 时
```

`manifest.json` 把 merged image、application、bootloader、partitions、
boot_app0、ELF、MAP 的路径、大小与 SHA256 绑在一起。刷机和崩溃符号匹配时，
以 manifest 里的 merged image / ELF 身份为准。

> **`full.bin` 从 `0x0` 连续写入，会写 `0x9000` 起的 NVS 区域和 `0xE000` 的
> otadata。** 但合并镜像在 NVS 区域是 0xFF 填充，实测刷完后校准仍保留；
> 不要依赖全量镜像清空 NVS。需要从零校准/清空设置时，先显式擦除 NVS
> 或执行 `erase_flash`；需要保留 NVS 时不要刷 `full.bin`：
> 用 `pio run -e guachat_dev -t upload`，或只把 `firmware.bin` 写到 `0x10000`。
> 早先日志中的 `nvs_open failed: NOT_FOUND` 只说明当时 NVS 中没有该命名空间，
> 不能单独证明是刷写清空的。

合并完成后，工具默认调用 `tools/verify_firmware.py` 做独立校验，不只看
esptool 的退出码。工具会保留 bootloader 头里的 flash mode，不会强制 QIO。

### 3.2 手工命令

```bash
PY=~/.platformio/penv/Scripts/python.exe
ESPTOOL=~/.platformio/packages/tool-esptoolpy/esptool.py
BOOT_APP0=~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin
ENV=guachat_dev

$PY $ESPTOOL --chip esp32s3 merge_bin \
  -o build/guachat-dev-full.bin \
  --flash_freq 80m --flash_size 16MB \
  0x0     .pio/build/$ENV/bootloader.bin \
  0x8000  .pio/build/$ENV/partitions.bin \
  0xe000  $BOOT_APP0 \
  0x10000 .pio/build/$ENV/firmware.bin
```

> **不要加 `--flash_mode qio`。**
> PlatformIO 生成的 bootloader 头是 **DIO**；强制 QIO 后设备会进入
> `TG0WDT_SYS_RST` 启动循环，表现为反复打印 ROM 日志、没有应用输出。
> 保留默认（不传 `--flash_mode`）即可，设备启动日志会显示 `mode:DIO`。

### 3.3 独立校验

```bash
python tools/verify_firmware.py                 # 校验构建产物与合并镜像
python tools/verify_firmware.py --image-only    # 只校验合并镜像（可用 --image 指定）
```

它不依赖 manifest 的自述，而是直接解析镜像：

- 镜像头 magic、芯片 ID（ESP32-S3）、Flash 容量（16 MB）；
- bootloader 的 flash mode 必须是 DIO，QIO 直接失败；
- 分区表 magic、MD5、区间重叠与越界，app 分区必须容纳应用；
- 应用描述符 magic，以及描述符内嵌的 ELF SHA256 与同名 ELF 一致；
- `0x0` / `0x8000` / `0xE000` / `0x10000` 四个偏移处的字节与构建目录一致。

manifest 里的 SHA256 只是记录，不能代替上述校验。完整门禁
（`python tools/validate.py --all`）也会调用同一个校验器。

### 3.4 发布归档

```bash
python tools/archive_firmware.py create --env guachat_dev
python tools/archive_firmware.py verify dist/firmware/<sha256>
```

归档目录以合并镜像的 SHA256 命名：

```text
dist/firmware/<sha256>/
  guachat-dev-full.bin
  guachat-dev-full.bin.sha256
  guachat-dev-full.manifest.json
  firmware.elf
  firmware.map
  README.txt
  archive.json
```

`create` 会先校验构建，再复制 image / checksum / manifest / ELF / MAP，
最后重新校验；目标已存在且校验通过时保持幂等。`verify` 检查目录名等于镜像
SHA256、没有多余文件、每个文件的大小与 SHA256、源 manifest 与镜像一致，
并用 `verify_firmware.py` 再次核对 ELF 身份。归档约 32 MB（ELF + MAP 占大头），
`dist/` 已被 `.gitignore` 忽略，不要提交。

### 3.5 CI（仅 GitHub）

`.github/workflows/validate.yml` 在每次 push / PR 运行 `validate.py --static --host`；
`.github/workflows/firmware.yml` 在源码变化时构建 `guachat_dev`、合并 `0x0` 镜像、
生成归档，并上传镜像、校验和、manifest、恢复包与 `dist/firmware/**`（保留 14 天）。
两个 workflow 都固定了 Action 的完整 commit SHA。

Gitee 不会执行 GitHub Actions。Gitee 镜像上的验证必须在本地跑
`python tools/validate.py --all`，或在 Gitee Go 中另配流水线。

---

## 4. 烧录

### 4.1 直接烧 PlatformIO 产物

```bash
pio run -e guachat_dev -t upload
```

可以指定端口：

```bash
pio run -e guachat_dev -t upload --upload-port COM9
```

`platformio.ini` 没有固定 `upload_port`，脚本应自动探测。

### 4.2 烧合并固件

```bash
python tools/flash.py build/guachat-dev-full.bin
```

工具会列出串口、要求确认、然后执行：

```bash
esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 build/guachat-dev-full.bin
```

这会写入 NVS 区域与 otadata，但不保证清空已有 NVS（本次实测校准仍保留）。
烧录前必须确认用户接受“可能丢失、也可能保留”的不确定状态；需要明确保留时改用
`pio run -e guachat_dev -t upload`，需要明确清空时先显式擦除 NVS。

### 4.3 烧录前必须确认

- Agent **必须**先获得用户明确同意。
- 确认目标板型号与 Flash 大小（16 MB）。
- 确认没有正在进行的串口监视器占用端口。

---

## 5. 串口日志

### 5.1 普通监视

```bash
pio device monitor -b 115200
```

### 5.2 抓启动日志（重要）

固件里调用了 `Serial.setTxTimeoutMs(0)`：没有主机读串口时，输出会被丢弃。
因此**先打开串口，再复位设备**，否则会看不到启动日志。

Python 示例（Windows COM9）：

```python
import time, serial

s = serial.Serial('COM9', 115200, timeout=0.2)
s.setDTR(False)
s.setRTS(True)          # 拉低 EN
time.sleep(0.15)
s.setRTS(False)         # 释放复位
time.sleep(0.4)

end = time.time() + 20
buf = b''
while time.time() < end:
    buf += s.read(4096)
s.close()
print(buf.decode('utf-8', 'replace'))
```

正常启动应看到：

```text
[SD] 挂载成功 ...
[BSP] sensors: ready
[LoRa] 初始化成功 ...
[BSP] hardware init done
```

如果反复出现 `rst:0x...` / `TG0WDT_SYS_RST`，先检查合并固件的 flash mode，
再看 [`troubleshooting.md`](troubleshooting.md)。

---

## 6. 分区表

`partitions_16MB_app.csv`：

| 名称 | 类型 | 偏移 | 大小 |
| --- | --- | ---: | ---: |
| `nvs` | data/nvs | `0x9000` | `0x5000` |
| `otadata` | data/ota | `0xe000` | `0x2000` |
| `app0` | app/ota_0 | `0x10000` | `0xEE0000`（14.875 MiB） |
| `spiffs` | data/spiffs | `0xEF0000` | `0x100000`（1 MiB） |
| `coredump` | data/coredump | `0xFF0000` | `0x10000` |

没有 `app1`，所以**不支持 OTA**。交付方式是 USB 刷写完整固件。

---

## 7. 恢复

### 7.1 回到开发基线

```bash
pio run -e guachat_dev -t upload
```

### 7.2 完全擦除（最后手段）

```bash
~/.platformio/penv/Scripts/python.exe \
  ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32s3 erase_flash
```

擦除会清空 NVS（包含传感器校准、设置）和 TF 卡之外的设备内数据。
执行前必须确认用户接受。

---

## 8. 常见构建/上传错误

| 错误 | 原因 | 处理 |
| --- | --- | --- |
| `multiple definition of setup()` | 新增目录带了第二个入口 | 只保留 `src/main.cpp` 一套 `setup()` |
| `pinyin_dp.h: No such file` | 打开了 LVGL 中文输入法 | `lv_conf.h` 中 `LV_USE_IME_PINYIN` 应为 0；需要输入法时自行实现并补齐依赖 |
| `Could not open COM9` | 端口被占用或刚复位 | 关闭监视器，等 1–2 秒重试 |
| `drom0_0_seg overflow` | 字体/常量表太大 | 子集字体、移 PSRAM、改 IROM |
| 上传后无输出 | 串口打开太晚 | 先开串口再复位 |
| 启动循环 | 合并时强制 QIO | 去掉 `--flash_mode qio` |
