#include "SDCardManager.h"
#include <sdmmc_cmd.h>
#include <esp_vfs_fat.h>
#include <driver/sdmmc_host.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "../bsp/bsp_pins.h"

// ============================================================
// 硬件引脚定义（1线模式：仅 CLK + CMD + D0）
// D1/D2/D3 释放为普通 GPIO
// ============================================================
static const gpio_num_t PIN_CLK  = (gpio_num_t)BSP_SD_PIN_CLK;
static const gpio_num_t PIN_CMD  = (gpio_num_t)BSP_SD_PIN_CMD;
static const gpio_num_t PIN_D0   = (gpio_num_t)BSP_SD_PIN_D0;

static const char* TEST_FILE_PATH = "0:/_speed_test.tmp";
static const char* SD_MOUNT_POINT = BSP_SD_MOUNT_POINT;

// ============================================================
// 静态实例
// ============================================================
SDCardManager* SDCardManager::_instance = nullptr;

SDCardManager* SDCardManager::getInstance() {
    if (!_instance) _instance = new SDCardManager();
    return _instance;
}

// ── 跨任务 SD 访问互斥 (FatFs FF_FS_REENTRANT=0 非重入) ──
// 递归互斥量：不同任务并发访问 SD 必须串行化，
// 否则破坏 FAT 表缓存 / sector 缓冲 → 崩溃或文件损坏。
static SemaphoreHandle_t s_sdMutex = nullptr;

void SDCardManager::lockSd() {
    if (!s_sdMutex) s_sdMutex = xSemaphoreCreateRecursiveMutex();
    if (s_sdMutex) xSemaphoreTakeRecursive(s_sdMutex, portMAX_DELAY);
}

void SDCardManager::unlockSd() {
    if (s_sdMutex) xSemaphoreGiveRecursive(s_sdMutex);
}

const char* SDCardManager::frStr(FRESULT fr) {
    static const char* str[] = {
        "OK", "DISK_ERR", "INT_ERR", "NOT_READY", "NO_FILE", "NO_PATH",
        "INVALID_NAME", "DENIED", "EXIST", "INVALID_OBJECT", "WRITE_PROTECTED",
        "INVALID_DRIVE", "NOT_ENABLED", "NO_FILESYSTEM", "MKFS_ABORTED",
        "TIMEOUT", "LOCKED", "NOT_ENOUGH_CORE", "TOO_MANY_OPEN_FILES", "INVALID_PARAMETER"
    };
    return (fr < sizeof(str)/sizeof(str[0])) ? str[fr] : "UNKNOWN";
}

// ============================================================
// DMA缓冲分配
// ============================================================
uint8_t* SDCardManager::allocDmaBuffer(size_t size) {
#if ENABLE_DMA_DEBUG
    Serial.printf("[SD][DMA] 请求%uB | 内部SRAM 空闲=%u 最大块=%u | DMA区 空闲=%u 最大块=%u | PSRAM 空闲=%u 最大块=%u\n",
        (unsigned)size,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
#endif

    // 1. 尝试PSRAM
    uint8_t* buf = (uint8_t*)heap_caps_malloc(size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
    if (buf) {
#if ENABLE_DMA_DEBUG
        Serial.printf("[SD] PSRAM DMA分配成功: %uB\n", size);
#endif
        return buf;
    }

#if ENABLE_DMA_DEBUG
    Serial.printf("[SD] PSRAM DMA不可用，降级为内部SRAM DMA\n");
#endif

    // 2. 降级内部SRAM
    buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (buf) {
#if ENABLE_DMA_DEBUG
        Serial.printf("[SD] 内部SRAM DMA分配成功: %uB\n", size);
#endif
        return buf;
    }

    Serial.printf("[SD] DMA分配失败！size=%u | 内部SRAM最大块=%u | DMA区最大块=%u | PSRAM最大块=%u\n",
        (unsigned)size,
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    return nullptr;
}

// ============================================================
// 清理测速资源（不重置结果变量）
// ============================================================
void SDCardManager::cleanupSpeedTest() {
    SdLockGuard lock;
    if (_st.buf) {
        free(_st.buf);
        _st.buf = nullptr;
    }
    if (_mounted && _st.fil.obj.fs) {  // 检查文件是否打开
        f_close(&_st.fil);
        f_unlink(TEST_FILE_PATH);
    }
    // 注意：不重置_st.result，保留结果供UI读取
    // 只重置运行状态
    _st.step = SpeedStep::IDLE;
    _st.index = 0;
    _st.t0 = 0;
    _st.blockSize = 0;
    _st.iters = 0;
    _st.totalBytes = 0;
}

void SDCardManager::resetSpeedTest() {
    cleanupSpeedTest();
    memset(&_st.result, 0, sizeof(_st.result));  // 重置结果变量
#if ENABLE_DMA_DEBUG
    Serial.println("[SD] 测速状态已重置");
#endif
}

// ============================================================
// 构造/析构
// ============================================================
SDCardManager::SDCardManager()
    : _mounted(false), _totalBytes(0), _freeBytes(0), _cardType("未知"), _busFreq(0), _card(nullptr) {
    memset(&_st, 0, sizeof(_st));
    // 跨核 SD 互斥锁在此创建 (懒加载单例, 首次 getInstance 时 FreeRTOS 已就绪)。
    // 提前创建避免 lockSd 里的懒初始化在双核首次并发调用时竞态。
    if (!s_sdMutex) s_sdMutex = xSemaphoreCreateRecursiveMutex();
}

SDCardManager::~SDCardManager() {
    if (_mounted) {
        esp_vfs_fat_sdmmc_unmount();
        sdmmc_host_deinit();
    }
    cleanupSpeedTest();
}

// ============================================================
// 初始化挂载
// ============================================================
bool SDCardManager::begin() {
    if (_mounted) return true;

    Serial.println("[SD] 初始化SDMMC 1线模式...");

    for (int retry = 0; retry < MOUNT_RETRY_TIMES; retry++) {
        if (retry > 0) {
            Serial.printf("[SD] 挂载重试第%d次...\n", retry + 1);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

        sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
        slot.width = 1;  // 1线模式，释放 D1/D2/D3
        slot.clk = PIN_CLK;
        slot.cmd = PIN_CMD;
        slot.d0 = PIN_D0;
        slot.flags &= ~SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        esp_vfs_fat_mount_config_t mount_cfg{};
        mount_cfg.format_if_mount_failed = false;
        mount_cfg.max_files = 20;
        mount_cfg.allocation_unit_size = DEFAULT_CLUSTER_KB * 1024;

        sdmmc_card_t* card = nullptr;
        esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_cfg, &card);
        if (ret == ESP_OK) {
            _mounted = true;
            _card = card;
            _totalBytes = (uint64_t)card->csd.capacity * card->csd.sector_size;
            _freeBytes = scanFreeBytes();  // 挂载时强制扫描一次 FAT，缓存真实剩余
            _busFreq = card->max_freq_khz * 1000;
            sdmmc_card_print_info(stdout, card);

            FATFS* fs = nullptr;
            DWORD fre_clust = 0;
            if (f_getfree("0:", &fre_clust, &fs) == FR_OK && fs != nullptr) {
                uint32_t real_cluster_size = fs->csize * fs->ssize;
                Serial.printf("[SD] 挂载成功 | 簇大小:%luKB | 可用:%lluMB\n",
                              real_cluster_size/1024, getFreeBytes()/(1024 * 1024));
            }

            // 打印根目录内容 (便于确认文件夹是否就位)
            Serial.println("[SD] 根目录内容:");
            DIR *d = opendir(SD_MOUNT_POINT);
            if (d) {
                struct dirent *e;
                while ((e = readdir(d)) != NULL) {
                    Serial.printf("  %s%s\n", e->d_name,
                                  (e->d_type == DT_DIR) ? "/" : "");
                }
                closedir(d);
            } else {
                Serial.println("  (无法打开根目录)");
            }

            return true;
        }

        if (retry == MOUNT_RETRY_TIMES - 1) {
            Serial.printf("[SD] 挂载失败: %s (0x%x)\n", esp_err_to_name(ret), ret);
        }
    }
    return false;
}

// ============================================================
// 容量查询
// ============================================================
uint64_t SDCardManager::getTotalBytes() const { return _totalBytes; }

uint64_t SDCardManager::getFreeBytes() const {
    if (!_mounted) return 0;
    return _freeBytes;  // 返回挂载时扫描的缓存值
}

uint64_t SDCardManager::getUsedBytes() const {
    if (!_mounted || !_totalBytes) return 0;
    return _totalBytes > _freeBytes ? _totalBytes - _freeBytes : 0;
}

uint64_t SDCardManager::scanFreeBytes() const {
    SdLockGuard lock;
    FATFS* fs = nullptr;
    DWORD fre_clust = 0;
    if (f_getfree("0:", &fre_clust, &fs) != FR_OK || !fs) return 0;
    // FAT32 的 FSInfo 空闲簇计数可能过期（PC 写入后未安全弹出），
    // 置为无效值强制 f_getfree 走"扫描 FAT 表"分支，得到真实剩余。
    fs->free_clst = 0xFFFFFFFF;
    if (f_getfree("0:", &fre_clust, &fs) != FR_OK) return 0;
    return (uint64_t)fre_clust * fs->csize * fs->ssize;
}

void SDCardManager::checkPresence() {
    if (!_mounted || !_card) return;
    if (sdmmc_get_status((sdmmc_card_t*)_card) == ESP_OK) return;

    Serial.println("[SD] 卡已拔出，卸载文件系统");
    cleanupSpeedTest();
    esp_vfs_fat_sdmmc_unmount();
    sdmmc_host_deinit();
    _mounted = false;
    _totalBytes = 0;
    _freeBytes = 0;
    _card = nullptr;
}

// ============================================================
// 直接卸载 (CD 引脚已知卡拔出, 无需 sdmmc_get_status 轮询)
// ============================================================
void SDCardManager::unmount() {
    if (!_mounted) return;
    Serial.println("[SD] CD 拔出, 卸载文件系统");
    cleanupSpeedTest();
    esp_vfs_fat_sdmmc_unmount();
    sdmmc_host_deinit();
    _mounted = false;
    _totalBytes = 0;
    _freeBytes = 0;
    _card = nullptr;
}

// ============================================================
// 测速开始
// ============================================================
void SDCardManager::speedTestBegin(uint32_t testSizeKB) {
    SdLockGuard lock;
    // 清理旧状态，但保留结果变量（不清零）
    if (_st.buf) {
        free(_st.buf);
        _st.buf = nullptr;
    }
    if (_mounted && _st.fil.obj.fs) {
        f_close(&_st.fil);
        f_unlink(TEST_FILE_PATH);
    }
    
    // 重置运行状态，但保留结果变量
    _st.step = SpeedStep::IDLE;
    _st.index = 0;
    _st.t0 = 0;

    if (!_mounted) {
        Serial.println("[SD] 测速开始失败: TF卡未挂载");
        _st.step = SpeedStep::ERROR;
        return;
    }

    _st.testSizeKB = testSizeKB;
    _st.totalBytes = testSizeKB * 1024;

    // 顺序块大小自适应：优先32KB（DMA区现在有~180KB连续块），
    // 万一碎片化严重则逐级降级（32K→16K→8K→4K）。小块阶段固定4KB。
    static const uint32_t seqBlockCandidates[] = {32768, 16384, 8192, 4096};
    _st.blockSize = 0;
    _st.buf = nullptr;
    for (uint32_t sz : seqBlockCandidates) {
        _st.buf = allocDmaBuffer(sz);
        if (_st.buf) { _st.blockSize = sz; break; }
    }
    if (!_st.buf) {
        Serial.println("[SD] 测速开始失败: 缓冲分配失败");
        _st.step = SpeedStep::ERROR;
        return;
    }

    _st.iters = _st.totalBytes / _st.blockSize;
    if (_st.iters == 0) {
        free(_st.buf); _st.buf = nullptr;
        Serial.println("[SD] 测速开始失败: 块大小或总大小不合理");
        _st.step = SpeedStep::ERROR;
        return;
    }

    Serial.printf("[SD] 测速开始: 总%uKB, 块%uKB, 迭代%u次\n",
                  testSizeKB, _st.blockSize/1024, _st.iters);

    memset(_st.buf, 0xAA, _st.blockSize);

    FRESULT fr = f_open(&_st.fil, TEST_FILE_PATH, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        Serial.printf("[SD] f_open失败: %d (%s)\n", fr, frStr(fr));
        free(_st.buf); _st.buf = nullptr;
        _st.step = SpeedStep::ERROR;
        return;
    }
    
    // 预分配
    fr = f_lseek(&_st.fil, _st.totalBytes - 1);
    if (fr != FR_OK) {
        Serial.printf("[SD] 预分配lseek失败: %d\n", fr);
        f_close(&_st.fil); free(_st.buf); _st.buf = nullptr;
        _st.step = SpeedStep::ERROR;
        return;
    }
    fr = f_truncate(&_st.fil);
    if (fr != FR_OK) {
        Serial.printf("[SD] 预分配truncate失败: %d\n", fr);
        f_close(&_st.fil); free(_st.buf); _st.buf = nullptr;
        _st.step = SpeedStep::ERROR;
        return;
    }
    fr = f_lseek(&_st.fil, 0);
    if (fr != FR_OK) {
        Serial.printf("[SD] 回到开头lseek失败: %d\n", fr);
        f_close(&_st.fil); free(_st.buf); _st.buf = nullptr;
        _st.step = SpeedStep::ERROR;
        return;
    }

    _st.t0 = millis();
    _st.step = SpeedStep::SEQ_WRITE;
    Serial.printf("[SD] %uKB顺序写开始\n", _st.blockSize/1024);
}

// ============================================================
// 测速单步（核心修复：每个阶段正确计算迭代次数）
// ============================================================
SDCardManager::SpeedStep SDCardManager::speedTestStep() {
    if (_st.step == SpeedStep::IDLE || _st.step == SpeedStep::DONE || _st.step == SpeedStep::ERROR)
        return _st.step;

    SdLockGuard lock;   // 本步 = 一次 FatFs 读写 (f_read/f_write + 阶段切换的 f_open/f_close/f_unlink)

    bool isWrite = (_st.step == SpeedStep::SEQ_WRITE || _st.step == SpeedStep::SMALL_WRITE);
    UINT bw = 0;
    FRESULT fr = isWrite ? 
        f_write(&_st.fil, _st.buf, _st.blockSize, &bw) :
        f_read(&_st.fil, _st.buf, _st.blockSize, &bw);

    if (fr != FR_OK || bw != _st.blockSize) {
        Serial.printf("[SD] 读写失败: %d (%s), 实际读写%uB\n", fr, frStr(fr), bw);
        cleanupSpeedTest();
        _st.step = SpeedStep::ERROR;
        return _st.step;
    }

    _st.index++;
    if (_st.index >= _st.iters) {
        uint32_t elapsed = millis() - _st.t0;
        if (elapsed == 0) elapsed = 1;
        f_close(&_st.fil);

        switch (_st.step) {
            case SpeedStep::SEQ_WRITE: {
                _st.result.seqWriteMs = elapsed;
                _st.result.seqWriteKBps = (float)_st.testSizeKB * 1000.0f / elapsed;
                Serial.printf("[SD] %uKB顺序写完成: %.2f MB/s (耗时:%ums)\n",
                              _st.blockSize/1024,
                              _st.result.seqWriteKBps / 1024.0f, elapsed);
                
                // 切换到读阶段
                if (f_open(&_st.fil, TEST_FILE_PATH, FA_READ) != FR_OK) {
                    cleanupSpeedTest();
                    _st.step = SpeedStep::ERROR;
                    return _st.step;
                }
                _st.index = 0;
                _st.t0 = millis();
                _st.step = SpeedStep::SEQ_READ;
                Serial.printf("[SD] %uKB顺序读开始\n", _st.blockSize/1024);
                break;
            }

            case SpeedStep::SEQ_READ: {
                _st.result.seqReadMs = elapsed;
                _st.result.seqReadKBps = (float)_st.testSizeKB * 1000.0f / elapsed;
                Serial.printf("[SD] %uKB顺序读完成: %.2f MB/s (耗时:%ums)\n",
                              _st.blockSize/1024,
                              _st.result.seqReadKBps / 1024.0f, elapsed);
                
                // 切换到小块写阶段：释放顺序(8KB)缓冲，分配4KB缓冲，重新计算迭代次数
                free(_st.buf);
                _st.blockSize = 4096;  // 4KB小块
                _st.iters = _st.totalBytes / _st.blockSize;  // ✅ 关键：重新计算迭代次数（256次）
                _st.buf = allocDmaBuffer(_st.blockSize);
                if (!_st.buf) {
                    f_unlink(TEST_FILE_PATH);
                    _st.step = SpeedStep::ERROR;
                    return _st.step;
                }
                memset(_st.buf, 0xAA, _st.blockSize);
                
                if (f_open(&_st.fil, TEST_FILE_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
                    free(_st.buf); _st.buf = nullptr;
                    f_unlink(TEST_FILE_PATH);
                    _st.step = SpeedStep::ERROR;
                    return _st.step;
                }
                f_lseek(&_st.fil, _st.totalBytes - 1);
                f_truncate(&_st.fil);
                f_lseek(&_st.fil, 0);
                
                _st.index = 0;
                _st.t0 = millis();
                _st.step = SpeedStep::SMALL_WRITE;
                Serial.println("[SD] 4KB小块写开始");
                break;
            }

            case SpeedStep::SMALL_WRITE: {
                _st.result.smallWriteMs = elapsed;
                _st.result.smallWriteKBps = (float)_st.testSizeKB * 1000.0f / elapsed;
                Serial.printf("[SD] 4KB小块写完成: %.2f MB/s (耗时:%ums)\n",
                              _st.result.smallWriteKBps / 1024.0f, elapsed);
                
                // 切换到小块读阶段（复用4KB缓冲，不需要重新分配）
                f_close(&_st.fil);
                if (f_open(&_st.fil, TEST_FILE_PATH, FA_READ) != FR_OK) {
                    free(_st.buf); _st.buf = nullptr;
                    f_unlink(TEST_FILE_PATH);
                    _st.step = SpeedStep::ERROR;
                    return _st.step;
                }
                _st.index = 0;
                _st.t0 = millis();
                _st.step = SpeedStep::SMALL_READ;
                Serial.println("[SD] 4KB小块读开始");
                break;
            }

            case SpeedStep::SMALL_READ: {
                _st.result.smallReadMs = elapsed;
                _st.result.smallReadKBps = (float)_st.testSizeKB * 1000.0f / elapsed;
                Serial.printf("[SD] 4KB小块读完成: %.2f MB/s (耗时:%ums)\n",
                              _st.result.smallReadKBps / 1024.0f, elapsed);
                
                // 全部完成
                free(_st.buf); _st.buf = nullptr;
                f_unlink(TEST_FILE_PATH);
                _st.step = SpeedStep::DONE;
                Serial.println("[SD] 增量测速全部完成");
                break;
            }

            default: break;
        }
    }
    return _st.step;
}