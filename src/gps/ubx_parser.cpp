#include "ubx_parser.h"
#include "ubx_messages.h"
#include <Arduino.h>

#include "bsp/bsp_pins.h"

// ============================================================
// 编译期结构体大小断言 (UBX 协议要求精确 packing)
// ============================================================
#define UBX_ASSERT_SIZE(type, expected)                     \
    typedef char UBX_SIZEOF_##type[(sizeof(type) == (expected)) ? 1 : -1]

UBX_ASSERT_SIZE(ubx_nav_pvt_t, 90);
UBX_ASSERT_SIZE(ubx_nav_dop_t, 18);
UBX_ASSERT_SIZE(ubx_nav_eoe_t, 4);
UBX_ASSERT_SIZE(ubx_nav_odo_t, 20);
UBX_ASSERT_SIZE(ubx_nav_timeutc_t, 20);
UBX_ASSERT_SIZE(ubx_nav_sat_header_t, 8);
UBX_ASSERT_SIZE(ubx_nav_sat_sv_t, 12);

// ============================================================
// 全局共享实例
// ============================================================
gps_data_t g_gpsData;
volatile bool g_gpsFullMode = false;
int gpsModuleStatus = 0;  // 0=正常, 1=配置中, -1=未找到

// ============================================================
// UBX 状态机内部状态
// 所有 static 变量仅由 GPS 任务(单任务)访问，无需保护
// ============================================================
typedef enum {
    ST_SYNC1,
    ST_SYNC2,
    ST_CLASS,
    ST_ID,
    ST_LEN_L,
    ST_LEN_H,
    ST_PAYLOAD,
    ST_CK_A,
    ST_CK_B
} ubx_state_t;

static ubx_state_t _state = ST_SYNC1;
static uint8_t  _cls, _id;
static uint16_t _len;
static uint8_t  _payload[UBX_MAX_PAYLOAD];
static uint16_t _plIdx;
static uint8_t  _ckA, _ckB;   // Fletcher-8 累加器

// ============================================================
// 内部解析函数
// ============================================================

static void parseNAV_PVT(const uint8_t *p, uint16_t len) {
    if (len < sizeof(ubx_nav_pvt_t)) return;
    const ubx_nav_pvt_t *pvt = (const ubx_nav_pvt_t*)p;

    // 时间
    g_gpsData.year   = pvt->year;
    g_gpsData.month  = pvt->month;
    g_gpsData.day    = pvt->day;
    g_gpsData.hour   = pvt->hour;
    g_gpsData.min    = pvt->min;
    g_gpsData.sec    = pvt->sec;
    g_gpsData.iTOW   = pvt->iTOW;

    // 时间可用判据 (无定位时也能授时):
    //   valid bit0 日期 / bit1 时间 / bit2 UTC 完全解析(含闰秒), 且模块自报精度 tAcc < 1s。
    // 实测窗边 1~2 颗星时模块给 valid=0x37 flags2=0xA4 (confirmedDate=0), 但 tAcc≈0.12s —
    // 它自己的备份 RTC 一直在走, 只是"日期没被 GNSS 确认"。这种值比 PCF8563 漂几分钟靠谱得多,
    // 所以不再强制要求 confirmed 位; 只用 tAcc + 年份做合理性把关, 防止未授时模块写进垃圾。
    g_gpsData.timeValid = ((pvt->valid & 0x07) == 0x07) &&
                          (pvt->year >= 2020) &&
                          (pvt->tAcc < 1000000000UL);
    // 数据新鲜度时间戳: 睡眠期间 GPS 断电 / 串口停摆时 fixType 会一直停在旧值,
    // updateUI 靠它判断「数据是否还在更新」, 避免拿冻结时间刷屏、写回 RTC。
    g_gpsData.lastPvtMs = millis();

    // 从 iTOW 间隔计算实际 epoch 频率 (不受 millis() 漂移影响)
    static uint32_t _lastTow = 0;
    if (_lastTow != 0 && pvt->iTOW > _lastTow) {
        uint32_t delta = pvt->iTOW - _lastTow;
        if (delta >= 20 && delta <= 2000) {
            g_gpsData.hz = (1000 + delta / 2) / delta;  // 四舍五入
        }
    }
    _lastTow = pvt->iTOW;

    // 位置 (1e-7 deg → double)
    g_gpsData.lat = (double)pvt->lat * 1e-7;
    g_gpsData.lon = (double)pvt->lon * 1e-7;

    // 高度 (mm → 存 mm, 外部按需转换)
    g_gpsData.height = pvt->height;
    g_gpsData.hMSL   = pvt->hMSL;

    // 速度/航向
    g_gpsData.gSpeed = (uint32_t)pvt->gSpeed;  // mm/s
    g_gpsData.heading = pvt->heading;           // 1e-5 deg
    g_gpsData.velD    = pvt->velD;              // mm/s, 向下为正

    // 定位状态
    g_gpsData.fixType = pvt->fixType;
    g_gpsData.numSV   = pvt->numSV;

    // 精度
    g_gpsData.hAcc = pvt->hAcc;  // mm
    g_gpsData.vAcc = pvt->vAcc;  // mm
}

static void parseNAV_SAT(const uint8_t *p, uint16_t len) {
    if (len < sizeof(ubx_nav_sat_header_t)) return;
    const ubx_nav_sat_header_t *hdr = (const ubx_nav_sat_header_t*)p;

    uint8_t numSvs = hdr->numSvs;
    if (numSvs > UBX_MAX_SVS) numSvs = UBX_MAX_SVS;
    if (len < sizeof(ubx_nav_sat_header_t) + numSvs * sizeof(ubx_nav_sat_sv_t)) {
        numSvs = (len - sizeof(ubx_nav_sat_header_t)) / sizeof(ubx_nav_sat_sv_t);
        if (numSvs > UBX_MAX_SVS) numSvs = UBX_MAX_SVS;
    }

    g_gpsData.numSvs = numSvs;
    for (uint8_t i = 0; i < numSvs; i++) {
        const ubx_nav_sat_sv_t *sv =
            (const ubx_nav_sat_sv_t*)(p + sizeof(ubx_nav_sat_header_t) + i * sizeof(ubx_nav_sat_sv_t));
        g_gpsData.sats[i].gnssId = sv->gnssId;
        g_gpsData.sats[i].svId   = sv->svId;
        g_gpsData.sats[i].cno    = sv->cno;
        g_gpsData.sats[i].elev   = sv->elev;
        // 信号被接收机使用 = svUsed (bit0)
        // 注意: M10 上此标志包括定位候选星, 不一定是当前定位解算使用的
        //       实际定位星数为 NAV-PVT.numSV
        g_gpsData.sats[i].used = (sv->flags & 0x01) != 0;
    }

}

static void parseNAV_DOP(const uint8_t *p, uint16_t len) {
    if (len < sizeof(ubx_nav_dop_t)) return;
    const ubx_nav_dop_t *dop = (const ubx_nav_dop_t*)p;
    g_gpsData.hDOP = dop->hDOP;
}

static void parseNAV_ODO(const uint8_t *p, uint16_t len) {
    if (len < sizeof(ubx_nav_odo_t)) return;
    const ubx_nav_odo_t *odo = (const ubx_nav_odo_t*)p;
    g_gpsData.totalDistance = odo->totalDistance;
}

static void parseNAV_TIMEUTC(const uint8_t *p, uint16_t len) {
    if (len < sizeof(ubx_nav_timeutc_t)) return;
    const ubx_nav_timeutc_t *utc = (const ubx_nav_timeutc_t*)p;
    // 仅当 UTC 时间有效时覆盖 PVT 中的时间 (PVT 已经带了 UTC 时间)
    if (utc->valid & 0x01) {
        g_gpsData.year  = utc->year;
        g_gpsData.month = utc->month;
        g_gpsData.day   = utc->day;
        g_gpsData.hour  = utc->hour;
        g_gpsData.min   = utc->min;
        g_gpsData.sec   = utc->sec;
    }
}

static void onEpochComplete(void) {
    // Hz 已由 parseNAV_PVT 从 iTOW delta 计算，此处只做 epoch 同步
    g_gpsData.epochReady = true;
}

// ============================================================
// 消息分发
// ============================================================
static void ubx_dispatch(uint8_t cls, uint8_t id, const uint8_t *payload, uint16_t len) {
    if (cls == UBX_CLASS_NAV) {
        switch (id) {
            // 基础位置/时间 — 始终解析 (后台/前台都要用)
            case UBX_NAV_PVT:     parseNAV_PVT(payload, len);    break;
            // Epoch 结束标记 — 始终解析 (epochReady 同步)
            case UBX_NAV_EOE:     onEpochComplete();             break;
            // DOP — 始终解析 (室内判定 + 校准门槛依赖实时 hDOP)
            case UBX_NAV_DOP:     parseNAV_DOP(payload, len);    break;
            // 以下仅在 GPS 全解析模式时解析 (GPS 屏幕活跃)
            case UBX_NAV_SAT:     if (g_gpsFullMode) parseNAV_SAT(payload, len);    break;
            case UBX_NAV_ODO:     if (g_gpsFullMode) parseNAV_ODO(payload, len);    break;
            case UBX_NAV_TIMEUTC: if (g_gpsFullMode) parseNAV_TIMEUTC(payload, len);break;
            // 忽略其他 NAV 消息
        }
    }
    // 忽略非 NAV 消息 (CFG, MON, ...)
}

// ============================================================
// 公共 API
// ============================================================

void ubx_encode(uint8_t c) {
    switch (_state) {

    case ST_SYNC1:
        if (c == UBX_SYNC_CHAR_1) _state = ST_SYNC2;
        break;

    case ST_SYNC2:
        if (c == UBX_SYNC_CHAR_2) {
            _state = ST_CLASS;
            _ckA = 0; _ckB = 0;
        } else {
            _state = ST_SYNC1;  // 重新同步
        }
        break;

    case ST_CLASS:
        _cls = c;
        _ckA += c; _ckB += _ckA;
        _state = ST_ID;
        break;

    case ST_ID:
        _id = c;
        _ckA += c; _ckB += _ckA;
        _state = ST_LEN_L;
        break;

    case ST_LEN_L:
        _len = c;
        _ckA += c; _ckB += _ckA;
        _state = ST_LEN_H;
        break;

    case ST_LEN_H:
        _len |= (uint16_t)c << 8;
        _ckA += c; _ckB += _ckA;
        if (_len > UBX_MAX_PAYLOAD) {
            _state = ST_SYNC1;   // 超长帧，丢弃
        } else if (_len == 0) {
            _state = ST_CK_A;    // 无载荷，直接等校验
        } else {
            _plIdx = 0;
            _state = ST_PAYLOAD;
        }
        break;

    case ST_PAYLOAD:
        _payload[_plIdx++] = c;
        _ckA += c; _ckB += _ckA;
        if (_plIdx >= _len) {
            _state = ST_CK_A;
        }
        break;

    case ST_CK_A:
        if (c == _ckA) {
            _state = ST_CK_B;
        } else {
            _state = ST_SYNC1;  // 校验 A 失败
        }
        break;

    case ST_CK_B:
        if (c == _ckB) {
            // 完整有效帧 → 分发
            ubx_dispatch(_cls, _id, _payload, _len);
        }
        _state = ST_SYNC1;  // 无论校验是否通过，回到初始
        break;
    }
}

void ubx_reset(void) {
    _state = ST_SYNC1;
    _plIdx = 0;
    _len = 0;
    g_gpsData.epochReady = false;
}

// ============================================================
// serial0Begin — 统一 Serial0 初始化 (RX 缓冲 2KB)
// 5/20Hz 时单 epoch 突发(NAV-PVT+SAT+DOP+ODO+EOE)约 650B, 远超默认 256B。
// 后台 UI 连续渲染可能饿死 gpsTask(prio1), 若突发恰落在渲染窗口内,
// 256B 缓冲会溢出丢字节 → 丢 epoch(Hz 抖动 5→3→2)。
// ============================================================
static void serial0Begin(unsigned long baud) {
    Serial0.setRxBufferSize(2048);
    Serial0.begin(baud, SERIAL_8N1, BSP_GPS_PIN_RX, BSP_GPS_PIN_TX);
}

// ============================================================
// UBX 指令发送 — 构建帧 + Fletcher-8 校验和 + Serial0 写入
// ============================================================
static void ubx_send_frame(uint8_t cls, uint8_t id,
                           const uint8_t *payload, uint16_t len) {
    uint8_t buf[6 + 256 + 2];  // header(6) + max_payload + ck(2)
    uint8_t idx = 0;

    buf[idx++] = 0xB5;
    buf[idx++] = 0x62;
    buf[idx++] = cls;
    buf[idx++] = id;
    buf[idx++] = len & 0xFF;
    buf[idx++] = (len >> 8) & 0xFF;

    uint8_t ck_a = 0, ck_b = 0;
    for (int i = 2; i < idx; i++) { ck_a += buf[i]; ck_b += ck_a; }

    for (uint16_t i = 0; i < len; i++) {
        uint8_t b = payload[i];
        buf[idx++] = b;
        ck_a += b; ck_b += ck_a;
    }

    buf[idx++] = ck_a;
    buf[idx++] = ck_b;

    Serial0.write(buf, idx);
    Serial0.flush();
    delay(10);  // 留给 GPS 处理时间
}

// ============================================================
// ubx_send_valset — 发送 CFG-VALSET 单键值对
// key: 4 字节配置键; value/valueBytes: 值及其字节数(1/2/4)
// layer: 0x01=BBR, 0x02=RAM, 0x03=RAM+BBR
// ============================================================
static void ubx_send_valset(uint32_t key, uint32_t value, uint8_t valueBytes, uint8_t layer) {
    uint8_t payload[4 + 4 + 4]; // header(4) + key(4) + max_value(4)
    uint8_t idx = 0;

    // Header
    payload[idx++] = 0x00;        // version
    payload[idx++] = layer;       // layers (1=BBR, 2=RAM, 3=both)
    payload[idx++] = 0x00;        // reserved
    payload[idx++] = 0x00;        // position

    // Key (U4 LE)
    payload[idx++] = (uint8_t)(key & 0xFF);
    payload[idx++] = (uint8_t)((key >> 8) & 0xFF);
    payload[idx++] = (uint8_t)((key >> 16) & 0xFF);
    payload[idx++] = (uint8_t)((key >> 24) & 0xFF);

    // Value (LE, valueBytes: 1, 2, or 4)
    for (uint8_t i = 0; i < valueBytes; i++) {
        payload[idx++] = (uint8_t)(value >> (i * 8));
    }

    ubx_send_frame(0x06, 0x8A, payload, idx);
}

// ============================================================
// ubx_send_config_cmds — 发送 UBX 模式配置指令 (不含波特率)
// 在 ubx_configure 中调用, 可在 38400 或 230400 下工作
// ============================================================
static void ubx_send_config_cmds(void) {
    // 清空接收缓冲, 防止残留数据干扰
    while (Serial0.available()) Serial0.read();
    delay(20);

    // 全部使用 CFG-VALSET (0x06 0x8A), layer=0x03 (RAM+BBR) 一步到位

    // ① 频率 1Hz: key=0x30210001 (CFG_RATE_MEAS), value=1000ms (U2)
    ubx_send_valset(0x30210001, 1000, 2, 0x03);

    // ② 关闭 NMEA 输入解析: key=0x10730002, value=0 (U1)
    ubx_send_valset(0x10730002, 0, 1, 0x03);

    // ③ 关闭 NMEA 端口输出: key=0x10740002, value=0 (U1)
    ubx_send_valset(0x10740002, 0, 1, 0x03);

    // ④ 启用 UBX 消息输出 (各 rate=1)
    const uint32_t msg_keys[] = {
        0x20910007,  // NAV-PVT
        0x20910016,  // NAV-SAT
        0x20910039,  // NAV-DOP
        0x2091007F,  // NAV-ODO
        0x20910160,  // NAV-EOE
    };
    for (int i = 0; i < 5; i++) {
        ubx_send_valset(msg_keys[i], 1, 1, 0x03);
    }

    // ⑤ 关闭全部 NMEA 消息输出 (各 msgID rate=0, layer=0x03)
    //    对应 U-Center2 的 CFG_MSGOUT NMEA_ID_* 禁用
    const uint16_t nmea_off_ids[] = {
        0xBA, 0xBE, 0xBB, 0xC9, 0xCD, 0xCA,
        0xBF, 0xC3, 0xC0, 0xC4, 0xC8, 0xC5,
        0xAB, 0xAF, 0xAC, 0xB0, 0xB4, 0xB1,
    };
    for (int i = 0; i < (int)(sizeof(nmea_off_ids)/sizeof(nmea_off_ids[0])); i++) {
        ubx_send_valset(0x20910000 | nmea_off_ids[i], 0, 1, 0x03);
    }

    // ⑥ 关闭 SABS 防欺骗测距: key=0x10360003, value=0 (U1)
    ubx_send_valset(0x10360003, 0, 1, 0x03);

    // ⑦ FIXMODE 自动 (2D/3D 自动切换): key=0x20110011, value=3 (U1)
    ubx_send_valset(0x20110011, 3, 1, 0x03);

    // ⑧ 星座组合: 默认亚太 (mode=1 = BDS+GPS+QZSS)
    ubx_set_constellations(1);
}

// ============================================================
// ubx_send_baud_cmds — 发送波特率切换指令 (38400→230400)
// 在 ubx_configure 中确认模块在 38400 时调用
// ============================================================
static void ubx_send_baud_cmds(void) {
    // 波特率: 先 BBR (Flash 保存), 再 RAM (即时切换)
    // 注意: CFG-VALSET layer 值 — 0x01=RAM, 0x02=BBR, 0x03=RAM+BBR
    delay(100);  // 等之前 CFG-VALSET 的 BBR Flash 写完
    ubx_send_valset(0x40520001, 230400, 4, 0x02);  // BBR 先存 (层 0x02)
    delay(200);  // 等 GPS 写入 Flash
    ubx_send_valset(0x40520001, 230400, 4, 0x01);  // RAM 即时切换 (层 0x01)
    delay(100);  // 等 GPS 处理

    // GPS 已切到 230400, 同步 Serial0 (此时 BBR 已保存)
    Serial0.flush();
    Serial0.end();
    serial0Begin(230400);
    delay(100);
    while (Serial0.available()) Serial0.read();
}

// ============================================================
// ubx_configure — 启动时自动检测 + 配置 MAX-M10S
//   检测顺序: 230400(1.5s) → 115200(1.5s) → 38400(2s)
//   230400 UBX  → 跳过 (BBR 已配 230400)
//   230400 NMEA → 配置 UBX 但不切波特率
//   115200      → 旧 BBR (115200), 重配 + 切到 230400
//   38400       → 出厂状态, 完整配置 + 切到 230400
// ============================================================
void ubx_configure(void) {
    gpsModuleStatus = 1;

    // ─── 第一步: 检测 230400 (目标波特率, BBR 有效) ──────
    unsigned long start = millis();
    bool found230kUbx = false;
    bool found230kNmea = false;
    while (millis() - start < 1500) {
        if (Serial0.available()) {
            uint8_t c = Serial0.read();
            if (c == 0xB5) {
                found230kUbx = true;
                break;
            }
            if (c == '$') {
                found230kNmea = true;
                break;
            }
        }
        delay(10);
    }

    if (found230kUbx) {
        Serial.println("[GPS] 230400 UBX, 已配置");
        gpsModuleStatus = 0;
        return;
    }

    if (found230kNmea) {
        Serial.println("[GPS] 230400 NMEA → 重新配置 UBX (保持 230400)");
        ubx_send_config_cmds();
        ubx_reset();
        gpsModuleStatus = 0;
        Serial.println("[GPS] 配置完成 (230400 NMEA→UBX)");
        return;
    }

    // ─── 第二步: 检测 115200 (旧 BBR 残留) ──────
    Serial.println("[GPS] 切 115200 检测...");
    Serial0.flush();
    Serial0.end();
    serial0Begin(115200);
    delay(50);
    while (Serial0.available()) Serial0.read();

    start = millis();
    bool found115k = false;
    while (millis() - start < 1500) {
        if (Serial0.available()) {
            uint8_t c = Serial0.read();
            if (c == '$' || c == 0xB5) {
                found115k = true;
                break;
            }
        }
        delay(10);
    }

    if (found115k) {
        // GPS 在 115200 → 旧 BBR 配置, 完整重配 + 切到 230400
        Serial.println("[GPS] 115200 检测到 → 完整配置 + 切换 230400");
        ubx_send_config_cmds();
        ubx_send_baud_cmds();
        delay(100);
        ubx_reset();
        gpsModuleStatus = 0;
        Serial.println("[GPS] 配置完成 (115200→230400 UBX)");
        return;
    }

    // ─── 第三步: 切 38400 检测 (出厂/掉电状态) ──────
    Serial.println("[GPS] 切 38400 检测...");
    Serial0.flush();
    Serial0.end();
    serial0Begin(38400);
    delay(50);
    while (Serial0.available()) Serial0.read();

    start = millis();
    bool foundAt38400 = false;
    while (millis() - start < 2000) {
        if (Serial0.available()) {
            uint8_t c = Serial0.read();
            if (c == '$' || c == 0xB5) {
                foundAt38400 = true;
                break;
            }
        }
        delay(10);
    }

    if (!foundAt38400) {
        // 三个波特率都没数据 → GPS 异常
        Serial.println("[GPS] 未检测到 GPS 模块!");
        gpsModuleStatus = -1;
        Serial0.flush();
        Serial0.end();
        serial0Begin(230400);
        return;
    }

    // ─── 模块在 38400 → 完整配置 (含波特率切换) ──────
    Serial.println("[GPS] 38400 检测到, 发送完整配置...");
    ubx_send_config_cmds();
    ubx_send_baud_cmds();

    delay(100);
    ubx_reset();
    gpsModuleStatus = 0;
    Serial.println("[GPS] 配置完成 (38400→230400 UBX)");
}

// ============================================================
// ubx_set_rate — 动态修改输出频率
// measRateMs: 测量间隔(毫秒), 500=2Hz, 200=5Hz, 1000=1Hz
// ============================================================
void ubx_set_rate(uint16_t measRateMs) {
    if (measRateMs < 50 || measRateMs > 5000) return;  // 防呆

    // CFG-VALSET (0x06 0x8A) with key 0x30210001 = CFG_RATE_MEAS
    // layer=0x03 (RAM+BBR), value = measRate (U2 LE)
    ubx_send_valset(0x30210001, measRateMs, 2, 0x03);

    Serial.printf("[GPS] 频率已设为 %dHz\n", 1000 / measRateMs);
}

// ============================================================
// ubx_set_dynmodel — 设置动态模型 (DYNMODEL)
// key=0x20110021 (CFG-NAVSPG-DYNMODEL), value=U1
// 0x03=步行, 0x04=车载, 0x0A=自行车, 0x06=飞行(<1g)
// 每值分别写入 RAM(0x01) + BBR(0x02)
// ============================================================
void ubx_set_dynmodel(uint8_t dynModel) {
    ubx_send_valset(0x20110021, dynModel, 1, 0x01);  // RAM
    ubx_send_valset(0x20110021, dynModel, 1, 0x02);  // BBR
    Serial.printf("[GPS] 动态模型已设为 %d\n", dynModel);
}

// ============================================================
// ubx_set_constellations — 动态修改星座组合 (多条 CFG-VALSET)
// 每条指令来自 U-Center2 验证的 MAX-M10S CFG-VALSET 信号级键值
// 每个键值对独立发送 → RAM(0x01) + BBR(0x02)
//
// MAX-M10S 使用 CFG-VALSET 逐信号启停, 不使用 CFG-GNSS
//   0x10310021 — GAL   E1B/C  (U1)
//   0x10310022 — BDS   B1C    (U1)
//   0x10310024 — QZSS  L1CA   (U1)
//   0x10310025 — GLO   L1     (U1)
//   0x1031000F / 0x1031000D — BDS 子信号 (亚太专用)
// GPS 始终开启 (不在指令中控制)
// ============================================================
typedef struct { uint32_t key; uint8_t val; } valsig_t;

// 星座模式表 — 每个模式对应一组 {键, 值}
// 终止标记 VALSIG_END
#define VALSIG_END {0xFFFFFFFF, 0}

// 模式 0: 基础 (GAL+GPS)
static const valsig_t MODE_BASE[] = {
    {0x10310021, 1},
    {0x10310022, 0},
    {0x10310024, 0},
    {0x10310025, 0},
    VALSIG_END
};
// 模式 1: 亚太 (BDS+GPS+QZSS)
static const valsig_t MODE_ASIA[] = {
    {0x10310021, 0},
    {0x1031000F, 1},
    {0x1031000D, 0},
    {0x10310022, 1},
    {0x10310024, 1},
    {0x10310025, 0},
    VALSIG_END
};
// 模式 2: 中低纬 (BDS+GAL+GPS)
static const valsig_t MODE_MIDLAT[] = {
    {0x10310021, 1},
    {0x10310022, 1},
    {0x10310024, 0},
    {0x10310025, 0},
    VALSIG_END
};
// 模式 3: 北高纬 (GLO+GAL+GPS)
static const valsig_t MODE_HIGHLAT[] = {
    {0x10310021, 1},
    {0x10310022, 0},
    {0x10310024, 0},
    {0x10310025, 1},
    VALSIG_END
};
// 模式 4: 全球飞行 (GAL+GPS) — 同基础
static const valsig_t MODE_FLT_GLOBAL[] = {
    {0x10310021, 1},
    {0x10310022, 0},
    {0x10310024, 0},
    {0x10310025, 0},
    VALSIG_END
};
// 模式 5: 亚太飞行 (BDS+GPS)
static const valsig_t MODE_FLT_ASIA[] = {
    {0x10310021, 0},
    {0x10310022, 1},
    {0x10310024, 0},
    {0x10310025, 0},
    VALSIG_END
};

// 模式查找表
static const valsig_t* const MODE_TABLES[] = {
    MODE_BASE,       // 0
    MODE_ASIA,       // 1
    MODE_MIDLAT,     // 2
    MODE_HIGHLAT,    // 3
    MODE_FLT_GLOBAL, // 4
    MODE_FLT_ASIA,   // 5
};
#define NUM_CONST_MODES (sizeof(MODE_TABLES) / sizeof(MODE_TABLES[0]))

// ============================================================
void ubx_set_constellations(int modeIdx) {
    if (modeIdx < 0 || modeIdx >= (int)NUM_CONST_MODES) {
        Serial.printf("[GPS] 无效星座模式索引: %d\n", modeIdx);
        return;
    }

    const valsig_t *table = MODE_TABLES[modeIdx];
    for (int i = 0; table[i].key != 0xFFFFFFFF; i++) {
        ubx_send_valset(table[i].key, table[i].val, 1, 0x01);  // RAM
        ubx_send_valset(table[i].key, table[i].val, 1, 0x02);  // BBR
    }

    Serial.printf("[GPS] 星座模式已更新, mode=%d\n", modeIdx);
}
