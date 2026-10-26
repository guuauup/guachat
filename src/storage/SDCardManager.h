#ifndef SD_CARD_MANAGER_H
#define SD_CARD_MANAGER_H

#include <Arduino.h>
#include <stdint.h>
#include <ff.h>

// ==================== 可配置参数（按需修改，不用动其他代码）====================
#define FORCE_PSRAM_DMA     0   // 1=强制用PSRAM，失败直接报错；0=PSRAM失败降级SRAM（推荐0）
#define DEFAULT_CLUSTER_KB  64  // 默认 64 KB 簇；改成 16 即为 16 KB 簇
#define MOUNT_RETRY_TIMES   3   // 插拔挂载重试次数
#define SPEED_TEST_SIZE_KB  1024// 单次测速大小（1MB，小体积不卡UI）
#define ENABLE_DMA_DEBUG    1   // 1=打印DMA分配详情；0=仅打印错误（推荐0，不刷屏）
// ==============================================================================

/**
 * ESP32-S3 SDMMC 4线模式 TF卡管理
 * 适配Arduino-ESP32 2.0.x（IDF4.4底子），无需升级Core
 */
class SDCardManager {
public:
    static SDCardManager* getInstance();
    bool begin();                     // 初始化挂载（含插拔重试）
    bool isMounted() const { return _mounted; }
    uint64_t getTotalBytes() const;   // 总容量（字节）
    uint64_t getFreeBytes() const;    // 剩余容量（字节）
    uint64_t getUsedBytes() const;    // 已用容量（字节）
    const char* getCardType() const { return _cardType; }
    uint32_t getBusFreq() const { return _busFreq; } // 总线频率（Hz）
    void* getCard() const { return _card; }  // sdmmc_card_t* — 供 USB MSC 桥接使用
    void checkPresence();              // 热插拔检测 (sdmmc_get_status)
    void unmount();                    // 直接卸载 (CD 引脚知道卡已拔出)

    // 测速结果（内部存KB/s，对外显示转MB/s）
    struct SpeedResult {
        float seqWriteKBps = 0;  // 顺序写速度（KB/s，8KB块）
        float seqReadKBps  = 0;  // 顺序读速度（KB/s，8KB块）
        float smallWriteKBps = 0;// 4KB小块写速度（KB/s）
        float smallReadKBps  = 0;// 4KB小块读速度（KB/s）
        uint32_t seqWriteMs = 0; // 顺序写耗时（ms）
        uint32_t seqReadMs  = 0; // 顺序读耗时（ms）
        uint32_t smallWriteMs = 0;// 4KB写耗时（ms）
        uint32_t smallReadMs  = 0;// 4KB读耗时（ms）
    };

    // 非阻塞增量测速（支持连续调用，无需重启）
    enum class SpeedStep : uint8_t {
        IDLE, SEQ_WRITE, SEQ_READ, SMALL_WRITE, SMALL_READ, DONE, ERROR
    };
    void speedTestBegin(uint32_t testSizeKB = SPEED_TEST_SIZE_KB);
    SpeedStep speedTestStep();
    bool speedTestRunning() const {
        return _st.step != SpeedStep::IDLE && _st.step != SpeedStep::DONE;
    }
    const SpeedResult& speedTestResult() const { return _st.result; }
    void resetSpeedTest();  // 手动重置测速状态（备用接口）

    // ── 跨任务 SD 访问互斥 ──────────────────────────────────────────────
    // FatFs 非重入 (FF_FS_REENTRANT=0)：任意两个任务并发访问 SD 都会破坏
    // FAT 缓存或导致崩溃，例如后台任务与 UI 任务同时读写。
    // 所有 fopen/fread/fwrite/fclose/remove/mkdir/opendir/readdir/stat 及 FatFs
    // 直调 (f_open/f_read/f_write/...) 都必须持此递归互斥锁 (SdLockGuard RAII)。
    // 递归锁：同任务内嵌套调用不自我死锁。
    static void lockSd();
    static void unlockSd();

private:
    SDCardManager();
    ~SDCardManager();
    SDCardManager(const SDCardManager&) = delete;
    SDCardManager& operator=(const SDCardManager&) = delete;

    static SDCardManager* _instance;
    bool _mounted;
    uint64_t _totalBytes;
    uint64_t _freeBytes;  // 挂载时扫描一次的缓存值（剩余字节）
    const char* _cardType;
    uint32_t _busFreq;
    void* _card;  // sdmmc_card_t*

    // 增量测速上下文
    struct SpeedTestCtx {
        SpeedStep step = SpeedStep::IDLE;
        uint32_t testSizeKB = SPEED_TEST_SIZE_KB;
        uint32_t totalBytes = 0;
        uint32_t blockSize = 32768;  // 顺序块首选32KB（分配失败时在 speedTestBegin 里自适应降级）
        uint32_t iters = 0;
        uint32_t index = 0;
        uint32_t t0 = 0;             // 当前阶段计时起点（关键！每个阶段重置）
        FIL fil;
        uint8_t* buf = nullptr;
        SpeedResult result;
    } _st;

    // 内部工具函数
    void cleanupSpeedTest();  // 清理测速资源，重置状态
    uint8_t* allocDmaBuffer(size_t size);  // DMA缓冲分配（优先PSRAM）
    const char* frStr(FRESULT fr);  // FatFs错误码转字符串
    uint64_t scanFreeBytes() const;  // 强制扫描 FAT 表，返回真实剩余字节
};

// RAII 守卫：构造上锁、析构解锁（递归互斥，同任务嵌套安全）。
// 用法：在任意访问 SD 的函数开头写 `SdLockGuard lock;`，函数返回时自动释放。
class SdLockGuard {
public:
    SdLockGuard()  { SDCardManager::lockSd(); }
    ~SdLockGuard() { SDCardManager::unlockSd(); }
    SdLockGuard(const SdLockGuard&) = delete;
    SdLockGuard& operator=(const SdLockGuard&) = delete;
};

#endif // SD_CARD_MANAGER_H