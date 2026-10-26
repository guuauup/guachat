#ifndef UBX_MESSAGES_H
#define UBX_MESSAGES_H

#include <stdint.h>

// ============================================================
// UBX 协议常量 — 用于状态机字节匹配
// ============================================================
#define UBX_SYNC_CHAR_1  0xB5
#define UBX_SYNC_CHAR_2  0x62

#define UBX_CLASS_NAV    0x01
#define UBX_CLASS_CFG    0x06

// NAV messages
#define UBX_NAV_PVT      0x07
#define UBX_NAV_SAT      0x35
#define UBX_NAV_DOP      0x04
#define UBX_NAV_EOE      0x61
#define UBX_NAV_ODO      0x09
#define UBX_NAV_TIMEUTC  0x14

// ============================================================
// 最大帧载荷 (NAV-SAT 每卫星 12 字节，最大 ~64 颗)
// ============================================================
#define UBX_MAX_PAYLOAD  1024
#define UBX_MAX_SVS      64

// ============================================================
// NAV-PVT (0x01 0x07) — 位置/速度/时间
// 载荷长度: 92 字节 (协议版本 27+)
// ============================================================
#pragma pack(push, 1)
typedef struct {
    uint32_t iTOW;          // 0
    int16_t  year;          // 4
    uint8_t  month;         // 6
    uint8_t  day;           // 7
    uint8_t  hour;          // 8
    uint8_t  min;           // 9
    uint8_t  sec;           // 10
    uint8_t  valid;         // 11
    uint32_t tAcc;          // 12
    int32_t  nano;          // 16
    uint8_t  fixType;       // 20
    uint8_t  flags;         // 21
    uint8_t  flags2;        // 22
    uint8_t  numSV;         // 23
    int32_t  lon;           // 24  (1e-7 deg)
    int32_t  lat;           // 28  (1e-7 deg)
    int32_t  height;        // 32  (mm)
    int32_t  hMSL;          // 36  (mm)
    uint32_t hAcc;          // 40  (mm)
    uint32_t vAcc;          // 44  (mm)
    int32_t  velN;          // 48  (mm/s)
    int32_t  velE;          // 52  (mm/s)
    int32_t  velD;          // 56  (mm/s)
    int32_t  gSpeed;        // 60  (mm/s)
    int32_t  heading;       // 64  (1e-5 deg)
    uint32_t sAcc;          // 68
    uint32_t headingAcc;    // 72
    uint16_t pDOP;          // 76  (0.01)
    uint8_t  flags3;        // 78
    uint8_t  reserved0[3];  // 79-81
    int32_t  headVeh;       // 82
    int16_t  magDec;        // 86
    uint16_t magAcc;        // 88
} ubx_nav_pvt_t;
// Expected total = 90 bytes; 收到时按 length 字段校验即可

// ============================================================
// NAV-DOP (0x01 0x04) — DOP 值
// 载荷长度: 18 字节
// ============================================================
typedef struct {
    uint32_t iTOW;          // 0
    uint16_t gDOP;          // 4  (0.01)
    uint16_t pDOP;          // 6
    uint16_t tDOP;          // 8
    uint16_t vDOP;          // 10
    uint16_t hDOP;          // 12
    uint16_t nDOP;          // 14
    uint16_t eDOP;          // 16
} ubx_nav_dop_t;

// ============================================================
// NAV-EOE (0x01 0x61) — Epoch 结束标记
// 载荷长度: 4 字节
// ============================================================
typedef struct {
    uint32_t iTOW;          // 0
} ubx_nav_eoe_t;

// ============================================================
// NAV-ODO (0x01 0x09) — 里程
// 载荷长度: 20 字节
// ============================================================
typedef struct {
    uint8_t  version;       // 0
    uint8_t  reserved0[3];  // 1-3
    uint32_t iTOW;          // 4
    uint32_t distance;      // 8  (m)
    uint32_t totalDistance; // 12 (m)
    uint32_t distanceStd;   // 16 (m)
} ubx_nav_odo_t;

// ============================================================
// NAV-TIMEUTC (0x01 0x14) — UTC 时间
// 载荷长度: 20 字节
// ============================================================
typedef struct {
    uint32_t iTOW;          // 0
    uint32_t tAcc;          // 4
    int32_t  nano;          // 8
    uint16_t year;          // 12
    uint8_t  month;         // 14
    uint8_t  day;           // 15
    uint8_t  hour;          // 16
    uint8_t  min;           // 17
    uint8_t  sec;           // 18
    uint8_t  valid;         // 19
} ubx_nav_timeutc_t;

// NAV-SAT 头 (固定 8 字节 + 每卫星 12 字节变长)
typedef struct {
    uint32_t iTOW;          // 0
    uint8_t  version;       // 4
    uint8_t  numSvs;        // 5
    uint8_t  reserved0[2];  // 6-7
    // uint8_t sv[] 变长跟在后面, 每 12 字节一个
} ubx_nav_sat_header_t;

typedef struct {
    uint8_t  gnssId;        // 0  0=GPS, 2=GAL, 3=BDS, 5=QZSS, 6=GLO
    uint8_t  svId;          // 1
    uint8_t  cno;           // 2  dBHz
    uint8_t  elev;          // 3  deg
    int16_t  azim;          // 4  deg
    int16_t  prRes;         // 6  0.1m
    uint32_t flags;         // 8
} ubx_nav_sat_sv_t;

#pragma pack(pop)

#endif // UBX_MESSAGES_H
