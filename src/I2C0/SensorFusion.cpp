/**
 * 9轴传感器融合 + 地磁计 4 参数硬铁球面校准
 *
 * 校准算法: 硬铁球面拟合 (最小二乘) + 均匀 scale
 *   球面方程: x²+y²+z² = a·x + b·y + c·z + d  (归一化坐标, ÷NORM 改善条件数)
 *   在线累加 4×4 AᵀA + Aᵀb, 三轴极差都 > MIN_RANGE 时自动完成 → 高斯消元求解:
 *   球心 o=(a/2,b/2,c/2) = 硬铁 offset, 半径 R → 均匀 scale = 1/R。
 *   相比 min/max: 球面拟合能从不完整的球面弧段估计球心, 不怕某轴没转满。
 */
#include "SensorFusion.h"
#include "LSM6DS3TRC.h"
#include <math.h>

SensorFusion* SensorFusion::_instance = nullptr;

SensorFusion* SensorFusion::getInstance() {
    if (!_instance) _instance = new SensorFusion();
    return _instance;
}

bool SensorFusion::begin() {
    _magReady = QMC5883PManager::getInstance()->begin();
    _imuReady = LSM6DS3TRCManager::getInstance()->begin();

    if (!_magReady && !_imuReady) {
        Serial.println("[Fusion] No sensors, fusion disabled");
        return false;
    }

    if (loadCalibFromNVS()) {
        QMC5883PManager::getInstance()->setCalibration(_calib);
    } else {
        Serial.println("[Fusion] No calib in NVS, raw magnetometer used");
    }

    // 每次开机打印一次当前校准参数 (便于跨环境诊断偏差)
    if (_calib.valid) {
        Serial.printf("[Fusion] Calib: off=(%.0f,%.0f,%.0f)\n"
                      "  W=[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]]\n",
                      _calib.offset_x, _calib.offset_y, _calib.offset_z,
                      _calib.W00, _calib.W01, _calib.W02,
                      _calib.W10, _calib.W11, _calib.W12,
                      _calib.W20, _calib.W21, _calib.W22);
    } else {
        Serial.println("[Fusion] Calib: NONE (raw magnetometer, offset=0 W=identity)");
    }

    _lastUpdateMs = millis();
    Serial.printf("[Fusion] mag=%d imu=%d\n", _magReady, _imuReady);
    return true;
}

// ── 运行时启停: 应用不需要姿态数据时调用 ──
void SensorFusion::setEnabled(bool en) {
    if (en && !_enabled) {
        // 重新启用: 重置融合 + 磁 LPF, 让航向立即以最新磁力计样本为准
        _firstUpdate = true;
        _magLPFInit = false;
    }
    _enabled = en;
}

// ============================================================
// 主更新
// ============================================================
float SensorFusion::update() {
    uint32_t now = millis();
    if (!_enabled) {
        _lastUpdateMs = now;   // 保持 dt 连续, 重新启用时不会出现超大 dt
        return _fusedHeading;
    }
    float dt = (now - _lastUpdateMs) / 1000.0f;
    _lastUpdateMs = now;
    if (dt <= 0 || dt > 1.0f) dt = 0.2f;

    // ── 读取传感器 ──
    float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
    float mx = 0, my = 0, mz = 0;
    bool accelOk = false, gyroOk = false, magOk = false;

    if (_imuReady) {
        accelOk = LSM6DS3TRCManager::getInstance()->readAccel(ax, ay, az);
        gyroOk  = LSM6DS3TRCManager::getInstance()->readGyro(gx, gy, gz);
    }
    if (_magReady) {
        if (_calib.valid) {
            magOk = QMC5883PManager::getInstance()->readCalibrated(mx, my, mz);
        } else {
            int16_t rx, ry, rz;
            magOk = QMC5883PManager::getInstance()->readRaw(rx, ry, rz);
            mx = (float)rx; my = (float)ry; mz = (float)rz;
        }
    }

    // ── 磁力计低通 (τ≈0.2s): raw 噪声经倾斜补偿会被放大, 先平滑再算航向 ──
    // 动态时主要靠陀螺仪积分, 磁力计只做慢校正, 此滞后可接受
    if (magOk) {
        if (!_magLPFInit) {
            _magLPF[0] = mx; _magLPF[1] = my; _magLPF[2] = mz;
            _magLPFInit = true;
        } else {
            float k = dt / (dt + 0.2f);
            _magLPF[0] += (mx - _magLPF[0]) * k;
            _magLPF[1] += (my - _magLPF[1]) * k;
            _magLPF[2] += (mz - _magLPF[2]) * k;
        }
        mx = _magLPF[0]; my = _magLPF[1]; mz = _magLPF[2];
    }

    // ── 陀螺仪零偏自动校准 (加速度静止检测) ──
    if (accelOk) {
        float amag = sqrtf(ax*ax + ay*ay + az*az);
        _amagFiltered = 0.9f * _amagFiltered + 0.1f * amag;
        _amagVar = 0.9f * _amagVar + 0.1f * (amag - _amagFiltered) * (amag - _amagFiltered);

        if (_amagVar < STATIONARY_VAR_THRESH) {
            _stationaryTicks++;
            _biasAccum += gz;
            if (_stationaryTicks >= STATIONARY_NEEDED) {
                float newBias = _biasAccum / (float)_stationaryTicks;
                // 平滑更新 bias, 避免突变
                _gyroBiasZ = 0.7f * _gyroBiasZ + 0.3f * newBias;
                _stationaryTicks = 0;
                _biasAccum = 0;
            }
        } else {
            _stationaryTicks = 0;
            _biasAccum = 0;
        }
    }

    // ── 校准模式: 采集样本 ──
    if (_calibrating) {
        if (magOk) {
            int16_t rx, ry, rz;
            QMC5883PManager::getInstance()->readRaw(rx, ry, rz);
            recordCalibSample(rx, ry, rz);
        }
        return _fusedHeading;
    }

    // ── 倾角 ──
    if (accelOk) computePitchRoll(ax, ay, az);

    // ── 磁力计航向 (磁北) ──
    float magHeading = _fusedHeading;
    if (magOk && accelOk) {
        magHeading = computeTiltCompensatedHeading(mx, my, mz, ax, ay, az);
    } else if (magOk) {
        magHeading = atan2f(-my, mx) * 180.0f / PI;
        if (magHeading < 0) magHeading += 360.0f;
    }

    // ── 互补滤波 ──
    float gyroZ_corrected = gz - _gyroBiasZ;   // 扣除零偏

    // 绕世界垂直轴的航向角速度 (Z-Y-X 欧拉角): yaw_rate = (sinφ·gy + cosφ·gz)/cosθ
    // 设备水平时退化为 gz; 倾斜时 gy 也贡献航向。
    // ⚠ 取负号: 陀螺 Z 右旋为正, 而磁北航向 atan2(-Yh,Xh) 是反方向增加,
    //   不取负会导致转动时航向先反向再被磁力计拽回 (实测"先反后纠")。
    float yawDeg;
    if (accelOk) {
        float cosPitchYaw = cosf(_pitch);
        if (cosPitchYaw < 0.3f) cosPitchYaw = 0.3f;   // 防万向锁 (pitch→±90° 时发散)
        yawDeg = -(sinf(_roll) * gy + cosf(_roll) * gyroZ_corrected) / cosPitchYaw * 180.0f / PI;
    } else {
        yawDeg = -gyroZ_corrected * 180.0f / PI;       // 无倾角信息, 退化为纯 gz
    }

    // ── 互补滤波系数 ──
    // 固定 alpha=0.95 (τ≈0.4s @50Hz): 转动靠陀螺积分瞬时跟上, 磁力计做慢校正。
    // 磁噪声已由前面的 LPF 抑制, 无需再靠"静止高 alpha 冻结"(那会带来 5~10s 拖尾);
    // 去掉"静止/运动"切换也避免了阈值附近 alpha 跳变导致的来回抖。
    const float alpha = 0.95f;

    if (_firstUpdate) {
        _fusedHeading = magHeading;
        _firstUpdate = false;
    } else if (gyroOk) {
        float predicted = _fusedHeading + yawDeg * dt;
        float error = magHeading - predicted;
        while (error > 180.0f)  error -= 360.0f;
        while (error < -180.0f) error += 360.0f;
        _fusedHeading = predicted + (1.0f - alpha) * error;
    } else if (magOk) {
        float error = magHeading - _fusedHeading;
        while (error > 180.0f)  error -= 360.0f;
        while (error < -180.0f) error += 360.0f;
        _fusedHeading = _fusedHeading + (alpha < 0.99f ? 0.3f : 0.05f) * error;
    }

    while (_fusedHeading < 0)       _fusedHeading += 360.0f;
    while (_fusedHeading >= 360.0f) _fusedHeading -= 360.0f;

    return _fusedHeading;
}

// ============================================================
// 倾角计算
// ============================================================
void SensorFusion::computePitchRoll(float ax, float ay, float az) {
    float norm = sqrtf(ax*ax + ay*ay + az*az);
    if (norm < 0.01f) return;
    ax /= norm; ay /= norm; az /= norm;

    _roll  = atan2f(ay, az);
    _pitch = atan2f(-ax, sqrtf(ay*ay + az*az));
}

// ============================================================
// 倾角补偿磁力计航向
// ============================================================
float SensorFusion::computeTiltCompensatedHeading(
        float mx, float my, float mz,
        float ax, float ay, float az) {

    float cosRoll  = cosf(_roll);
    float sinRoll  = sinf(_roll);
    float cosPitch = cosf(_pitch);
    float sinPitch = sinf(_pitch);

    // 倾斜补偿: 把机体磁场旋转到水平面 (roll 绕 X, pitch 绕 Y, R = Rx·Ry)
    //   Xh = mx·cosθ + mz·sinθ
    //   Yh = mx·sinφ·sinθ + my·cosφ − mz·sinφ·cosθ
    float Xh = mx * cosPitch + mz * sinPitch;
    float Yh = mx * sinRoll * sinPitch + my * cosRoll - mz * sinRoll * cosPitch;

    float heading = atan2f(-Yh, Xh) * 180.0f / PI;
    if (heading < 0) heading += 360.0f;
    return heading;
}

// ============================================================
// min/max 校准 — 重置极值
// ============================================================
void SensorFusion::calibReset() {
    for (int i = 0; i < 10; i++) _ata[i] = 0.0;
    for (int i = 0; i < 4;  i++) _atb[i] = 0.0;
    _min[0] = _min[1] = _min[2] =  32767;
    _max[0] = _max[1] = _max[2] = -32768;
}

// ============================================================
// min/max 校准 — 进度 0..1 (三轴极差达标度, 供 UI 显示)
// ============================================================
float SensorFusion::getCalibProgress() const {
    float p = 0.0f;
    for (int i = 0; i < 3; i++) {
        float a = (float)(_max[i] - _min[i]) / (float)MIN_RANGE;
        if (a > 1.0f) a = 1.0f;
        p += a;
    }
    return p / 3.0f;
}

// ============================================================
// 校准 — 启动
// ============================================================
void SensorFusion::startCalibration() {
    calibReset();
    _calibSampleCount = 0;
    _calibStartMs = millis();
    _calibDiagMs = 0;
    _calibrating = true;

    Serial.println("[Calib] Started hard-iron sphere fit — 画8字并缓慢翻转设备");
}

// ============================================================
// 校准 — 由调用方定期检查（诊断打印 + 超时）
// ============================================================
bool SensorFusion::updateCalibration() {
    if (!_calibrating) return false;

    uint32_t elapsed = millis() - _calibStartMs;

    // 诊断: 每 2 秒打印各轴极差, 观察哪根轴还没转够
    if (elapsed - _calibDiagMs >= 2000) {
        _calibDiagMs = elapsed;
        Serial.printf("[Calib] ranges X=%d Y=%d Z=%d samples=%d\n",
                      _max[0] - _min[0], _max[1] - _min[1], _max[2] - _min[2],
                      _calibSampleCount);
    }

    if (elapsed > 30000) {  // 30秒超时 → 用已采到的数据强制完成 (铺开不足则拒绝)
        Serial.println("[Calib] Timeout — finishing with collected data");
        finishCalibration();
        return false;
    }
    return true;
}

// ============================================================
// 校准 — 每帧采集 (累加球面方程 + 更新 min/max, 铺开达标自动完成)
// ============================================================
void SensorFusion::recordCalibSample(int16_t mx, int16_t my, int16_t mz) {
    // 更新每轴 min/max (铺开度/进度显示用)
    if (mx < _min[0]) _min[0] = mx;
    if (mx > _max[0]) _max[0] = mx;
    if (my < _min[1]) _min[1] = my;
    if (my > _max[1]) _max[1] = my;
    if (mz < _min[2]) _min[2] = mz;
    if (mz > _max[2]) _max[2] = mz;

    // 球面拟合累加 (归一化坐标, 改善条件数)
    const double NORM = 4096.0;
    double x = (double)mx / NORM;
    double y = (double)my / NORM;
    double z = (double)mz / NORM;

    double row[4] = { x, y, z, 1.0 };
    double tgt = x*x + y*y + z*z;

    int idx = 0;
    for (int i = 0; i < 4; i++)
        for (int j = i; j < 4; j++)
            _ata[idx++] += row[i] * row[j];
    for (int i = 0; i < 4; i++)
        _atb[i] += row[i] * tgt;

    _calibSampleCount++;

    // 铺开度: 三轴极差都达标 → 认为球面已覆盖足够 → 自动完成
    if ((_max[0] - _min[0] >= MIN_RANGE) &&
        (_max[1] - _min[1] >= MIN_RANGE) &&
        (_max[2] - _min[2] >= MIN_RANGE) &&
        _calibSampleCount > 200) {
        Serial.printf("[Calib] Enough sweep (%d,%d,%d) — auto-complete (%d samples)\n",
                      _max[0] - _min[0], _max[1] - _min[1], _max[2] - _min[2],
                      _calibSampleCount);
        finishCalibration();
    }
}

// ============================================================
// 4×4 高斯消元 (部分主元) 求解 A·x = b
// 返回 false = 奇异 (数据铺开不足, 球面拟合无唯一解)
// ============================================================
bool SensorFusion::solve4x4(double A[4][4], double b[4], double x[4]) {
    double M[4][5];
    double scale = 0.0;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            M[i][j] = A[i][j];
            double a = fabs(A[i][j]);
            if (a > scale) scale = a;
        }
        M[i][4] = b[i];
    }
    if (scale < 1e-12) return false;

    for (int col = 0; col < 4; col++) {
        // 部分主元: 找该列绝对值最大的行
        int piv = col;
        double maxAbs = fabs(M[col][col]);
        for (int r = col + 1; r < 4; r++) {
            double a = fabs(M[r][col]);
            if (a > maxAbs) { maxAbs = a; piv = r; }
        }
        if (maxAbs < scale * 1e-12) return false;   // 奇异

        if (piv != col) {
            for (int j = col; j < 5; j++) {
                double t = M[col][j]; M[col][j] = M[piv][j]; M[piv][j] = t;
            }
        }

        double diag = M[col][col];
        for (int r = col + 1; r < 4; r++) {
            double factor = M[r][col] / diag;
            if (factor == 0.0) continue;
            for (int j = col; j < 5; j++) M[r][j] -= factor * M[col][j];
        }
    }

    for (int i = 3; i >= 0; i--) {
        double s = M[i][4];
        for (int j = i + 1; j < 4; j++) s -= M[i][j] * x[j];
        x[i] = s / M[i][i];
    }
    return true;
}

// ============================================================
// 校准 — 完成: 4 参数球面拟合 → 硬铁 offset + 均匀 scale → 保存 NVS
// ============================================================
void SensorFusion::finishCalibration() {
    _calibrating = false;

    if (_calibSampleCount < 100) {
        Serial.printf("[Calib] Too few samples (%d), need >= 100 — aborted\n", _calibSampleCount);
        return;
    }

    // 还原 4×4 对称矩阵 A
    double A[4][4] = {0};
    int idx = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = i; j < 4; j++) {
            A[i][j] = _ata[idx];
            if (i != j) A[j][i] = _ata[idx];
            idx++;
        }
    }

    // 求解 p = [a,b,c,d]
    double p[4];
    if (!solve4x4(A, _atb, p)) {
        Serial.println("[Calib] ABORT: 球面拟合奇异 (铺开不足) — 请更大范围翻转, 保留旧校准");
        return;
    }

    // 球心 (归一化坐标)
    double ox = 0.5 * p[0];
    double oy = 0.5 * p[1];
    double oz = 0.5 * p[2];
    double r2 = p[3] + ox*ox + oy*oy + oz*oz;   // 半径² = d + |o|²
    if (r2 <= 0.0) {
        Serial.println("[Calib] ABORT: 非法半径 — 保留旧校准");
        return;
    }
    double R = sqrt(r2);

    // 反归一化到 raw counts
    const double NORM = 4096.0;
    double offX = ox * NORM;
    double offY = oy * NORM;
    double offZ = oz * NORM;
    double Rc   = R  * NORM;

    // 质量把关: 半径(场强)与偏移在合理范围, 否则拒绝 (强磁干扰/离群点)
    if (Rc < 1500.0 || Rc > 20000.0) {
        Serial.printf("[Calib] ABORT: 半径 %.0f counts 越界 — 保留旧校准\n", Rc);
        return;
    }
    double maxOff = fabs(offX);
    if (fabs(offY) > maxOff) maxOff = fabs(offY);
    if (fabs(offZ) > maxOff) maxOff = fabs(offZ);
    if (maxOff > 12000.0) {
        Serial.printf("[Calib] ABORT: 偏移 (%.0f,%.0f,%.0f) 越界 — 保留旧校准\n", offX, offY, offZ);
        return;
    }

    // 保存: offset 存 counts, W = 均匀 scale (1/Rc) → 归一化到单位球
    _calib.offset_x = (float)offX;
    _calib.offset_y = (float)offY;
    _calib.offset_z = (float)offZ;

    float s = (float)(1.0 / Rc);
    _calib.W00 = s; _calib.W01 = 0.0f; _calib.W02 = 0.0f;
    _calib.W10 = 0.0f; _calib.W11 = s; _calib.W12 = 0.0f;
    _calib.W20 = 0.0f; _calib.W21 = 0.0f; _calib.W22 = s;
    _calib.valid = true;

    Serial.printf("[Calib] hard-iron sphere fit done  samples=%d\n"
                  "  offset (counts): (%.0f, %.0f, %.0f)\n"
                  "  radius (counts): %.0f\n"
                  "  W diag: (%.4f, %.4f, %.4f)\n",
                  _calibSampleCount,
                  offX, offY, offZ,
                  Rc,
                  (double)_calib.W00, (double)_calib.W11, (double)_calib.W22);

    QMC5883PManager::getInstance()->setCalibration(_calib);
    saveCalibToNVS();

    _firstUpdate = true;  // 重置融合状态
}

void SensorFusion::cancelCalibration() {
    _calibrating = false;
    Serial.println("[Calib] Cancelled by user");
}

// ============================================================
// NVS 持久化
// ============================================================
bool SensorFusion::loadCalibFromNVS() {
    Preferences prefs;
    if (!prefs.begin("sensors", true)) return false;

    _calib.valid    = prefs.getBool("mag_valid", false);
    _calib.offset_x = prefs.getFloat("mag_ox", 0);
    _calib.offset_y = prefs.getFloat("mag_oy", 0);
    _calib.offset_z = prefs.getFloat("mag_oz", 0);

    if (prefs.isKey("mag_w00")) {
        // 新格式: 3×3 软铁矩阵 (min/max 法下为非对角 0 的对角矩阵)
        _calib.W00 = prefs.getFloat("mag_w00", 1);
        _calib.W01 = prefs.getFloat("mag_w01", 0);
        _calib.W02 = prefs.getFloat("mag_w02", 0);
        _calib.W10 = prefs.getFloat("mag_w10", 0);
        _calib.W11 = prefs.getFloat("mag_w11", 1);
        _calib.W12 = prefs.getFloat("mag_w12", 0);
        _calib.W20 = prefs.getFloat("mag_w20", 0);
        _calib.W21 = prefs.getFloat("mag_w21", 0);
        _calib.W22 = prefs.getFloat("mag_w22", 1);
    } else {
        // 旧格式 (对角 scale) → 迁移为对角矩阵 (非对角项 = 0)
        _calib.W00 = prefs.getFloat("mag_sx", 1);
        _calib.W01 = 0; _calib.W02 = 0;
        _calib.W10 = 0;
        _calib.W11 = prefs.getFloat("mag_sy", 1);
        _calib.W12 = 0;
        _calib.W20 = 0; _calib.W21 = 0;
        _calib.W22 = prefs.getFloat("mag_sz", 1);
    }
    prefs.end();

    return _calib.valid;
}

void SensorFusion::saveCalibToNVS() {
    Preferences prefs;
    if (!prefs.begin("sensors", false)) return;

    prefs.putBool("mag_valid", _calib.valid);
    prefs.putFloat("mag_ox", _calib.offset_x);
    prefs.putFloat("mag_oy", _calib.offset_y);
    prefs.putFloat("mag_oz", _calib.offset_z);
    prefs.putFloat("mag_w00", _calib.W00);
    prefs.putFloat("mag_w01", _calib.W01);
    prefs.putFloat("mag_w02", _calib.W02);
    prefs.putFloat("mag_w10", _calib.W10);
    prefs.putFloat("mag_w11", _calib.W11);
    prefs.putFloat("mag_w12", _calib.W12);
    prefs.putFloat("mag_w20", _calib.W20);
    prefs.putFloat("mag_w21", _calib.W21);
    prefs.putFloat("mag_w22", _calib.W22);
    prefs.end();

    Serial.println("[Fusion] Calib saved to NVS (hard-iron sphere)");
}

// ============================================================
// Light Sleep — 进入低功耗 / 唤醒恢复
// ============================================================
void SensorFusion::preSleep() {
    if (_magReady) QMC5883PManager::getInstance()->sleep();
    if (_imuReady) LSM6DS3TRCManager::getInstance()->sleep();
    Serial.println("[Fusion] Sensors → low power");
}

void SensorFusion::postWake() {
    if (_magReady) QMC5883PManager::getInstance()->wake();
    if (_imuReady) LSM6DS3TRCManager::getInstance()->wake();
    _firstUpdate = true;  // 唤醒后重新初始化融合状态
    Serial.println("[Fusion] Sensors restored");
}
