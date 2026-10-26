#ifndef UBX_PARSER_H
#define UBX_PARSER_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
// gps_data_t — 跨任务共享的 GPS 数据
// GPS 任务(写者, prio 1)与 UI 任务(读者, prio 5)通过此结
// 构体通信, 无需互斥锁:
//   写者仅在收到 NAV-EOE 时置 epochReady=true
//   读者仅在 epochReady=true 时读取(此时写者已写完本轮)
//   读者优先级高于写者, 读取期间写者不会运行
// ============================================================
#define UBX_MAX_SVS 64

// GNSS ID → 显示顺序映射 (用于卫星柱状图颜色)
#define GNSS_ID_GPS  0
#define GNSS_ID_GAL  2
#define GNSS_ID_BDS  3
#define GNSS_ID_QZSS 5
#define GNSS_ID_GLO  6

typedef struct {
    // --- NAV-PVT ---
    double lat;             // 度 (1e-7 原始转 double)
    double lon;             // 度
    int32_t height;         // 椭球高 mm
    int32_t hMSL;           // 海平面高 mm
    uint32_t gSpeed;        // 地面速度 mm/s
    int32_t heading;        // 航向 1e-5 deg
    int32_t velD;           // 垂直速度 mm/s (向下为正)
    uint8_t fixType;        // 0=NoFix, 2=2D, 3=3D
    uint8_t numSV;          // 使用中卫星数
    uint16_t year;          // UTC
    uint8_t month, day, hour, min, sec;
    bool timeValid;         // NAV-PVT valid 的 bit0(日期)+bit1(时间) 都有效
    volatile uint32_t lastPvtMs;  // 最近一次 NAV-PVT 到达时刻 millis() (0=还没收到过)

    // --- NAV-DOP ---
    uint16_t hDOP;          // 0.01

    // --- NAV-SAT ---
    uint8_t numSvs;         // 可见卫星总数
    struct {
        uint8_t gnssId;     // 0=GPS, 2=GAL, 3=BDS, 5=QZSS, 6=GLO
        uint8_t svId;
        uint8_t cno;        // dBHz
        uint8_t elev;       // 仰角 deg
        bool    used;   // true = 接收机正在跟踪此卫星信号 (svUsed bit0)
    } sats[UBX_MAX_SVS];

    // --- NAV-ODO ---
    uint32_t totalDistance; // 累计里程 (m)

    // --- 精度 (NAV-PVT hAcc/vAcc, mm) ---
    uint32_t hAcc;          // 水平精度 mm
    uint32_t vAcc;          // 垂直精度 mm

    // --- Hz 统计 ---
    uint8_t hz;             // 最近一秒 epoch 数
    uint32_t iTOW;          // 最近一次 iTOW (ms)

    // 一致性标记: true = 本轮 epoch 数据完整, 可读取
    volatile bool epochReady;
} gps_data_t;

// 全局共享实例 (ubx_parser.cpp 中定义)
extern gps_data_t g_gpsData;

// ============================================================
// API
// ============================================================

// 馈入一个 UBX 字节 (在 GPS 任务循环中调用)
void ubx_encode(uint8_t c);

// 获取最近一秒的 epoch 频率 (Hz)
static inline uint8_t ubx_get_hz(void) { return g_gpsData.hz; }

// 重置解析器状态 (例如串口重新连接后)
void ubx_reset(void);

// 启动时自动配置 MAX-M10S (检测 NMEA → 发送配置)
// 在 Serial0.begin() 之后, 创建 GPS 任务之前调用
void ubx_configure(void);

// 动态修改输出频率 (应用层可调用)
// measRateMs: 测量间隔(ms), 如 500=2Hz, 200=5Hz
// 发送 CFG-RATE 后自动保存到 BBR
void ubx_set_rate(uint16_t measRateMs);

// 动态修改动态模型 (DYNMODEL)
// dynModel: 3=步行, 4=车载, 10=自行车, 6=飞行(<1g)
// 分别写入 RAM+BBR
void ubx_set_dynmodel(uint8_t dynModel);

// 动态修改星座组合 — 按模式索引发送多条 CFG-VALSET
// modeIdx: 0=GPS+GAL, 1=GPS+BDS+QZSS, 2=GPS+GAL+BDS,
//          3=GPS+GLO+GAL, 4=GPS+GAL, 5=GPS+BDS
// 每个键值对分别发送到 RAM+BBR
void ubx_set_constellations(int modeIdx);

// GPS 模块状态 (由 ubx_configure 设置, gpsdata 读取)
extern int gpsModuleStatus;  // 0=正常, 1=配置中, -1=未找到

// GPS 全解析模式开关
//   false (默认): 只解析 NAV-PVT (经纬度/时间), 跳过 SAT/DOP/ODO
//   true:          解析全部 UBX 消息 (包括卫星明细用于柱状图)
// 由 GPSManager 根据当前屏幕自动切换
extern volatile bool g_gpsFullMode;

#endif // UBX_PARSER_H
