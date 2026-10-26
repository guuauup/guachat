/**
 * 9轴传感器融合 + 地磁计软铁/硬铁校准
 *
 * 融合 QMC5883P (磁力计) + LSM6DS3TRC (加速度计+陀螺仪)
 * 输出倾角补偿后的互补滤波航向角
 *
 * 校准: 4 参数硬铁球面拟合 (最小二乘) + 均匀 scale
 *   1. startCalibration() — 开始, 重置球面累加器 + 每轴 min/max
 *   2. recordCalibSample() — 累加 4×4 球面方程 + 更新三轴 min/max
 *   3. 三轴极差都 > MIN_RANGE 时自动完成 → 球心=硬铁 offset, 半径→均匀 scale → 保存 NVS
 */
#ifndef SENSOR_FUSION_H
#define SENSOR_FUSION_H

#include <Arduino.h>
#include <Preferences.h>
#include "QMC5883P.h"

class SensorFusion {
public:
    static SensorFusion* getInstance();

    bool begin();
    float update();

    // ── 运行时启停: 不需要姿态数据时关闭读取, 节省 I2C 总线与 CPU ──
    void setEnabled(bool en);
    bool isEnabled() const { return _enabled; }

    float getHeading() const { return _fusedHeading; }
    float getPitch()    const { return _pitch; }   // 弧度; 需要水平零点请在应用层处理
    float getRoll()     const { return _roll; }

    // 磁北航向 0..360°（非真北）
    float getMagneticHeading() const {
        float h = _fusedHeading;
        while (h < 0)       h += 360.0f;
        while (h >= 360.0f) h -= 360.0f;
        return h;
    }

    bool isSensorReady() const { return _magReady || _imuReady; }
    bool hasMag()        const { return _magReady; }
    bool hasIMU()        const { return _imuReady; }

    // ── 校准 ──
    bool isCalibrated()  const { return _calib.valid; }
    bool isCalibrating() const { return _calibrating; }
    float getCalibProgress() const;   // min/max 校准进度 0..1 (三轴极差达标度)
    const MagCalibData& getCalib() const { return _calib; }

    void startCalibration();
    bool updateCalibration();
    void cancelCalibration();

    bool loadCalibFromNVS();
    void saveCalibToNVS();

    // Light Sleep 前后调用（由应用在进入/退出低功耗时自行调用）
    void preSleep();
    void postWake();

private:
    SensorFusion() {}
    static SensorFusion* _instance;

    bool _magReady = false;
    bool _imuReady = false;
    MagCalibData _calib;

    float _fusedHeading = 0;
    float _pitch = 0, _roll = 0;
    uint32_t _lastUpdateMs = 0;
    bool _firstUpdate = true;

    bool _enabled = true;   // 传感器读取使能（应用不需要姿态数据时置 false 省 CPU）

    // ── 磁力计低通 (抑制 raw 噪声经倾斜补偿后的放大) ──
    float _magLPF[3] = {0, 0, 0};
    bool  _magLPFInit = false;

    static constexpr float ALPHA = 0.90f;

    // ── 陀螺仪零偏自动校准 ──
    // 加速度计幅值方差 < 阈值 → 设备静止 → 采集 gyroZ 均值 → 更新 bias
    float _gyroBiasZ = 0;          // 陀螺 Z 轴零偏 (rad/s)
    float _amagFiltered = 9.8f;    // 加速度幅值低通
    float _amagVar = 1.0f;         // 加速度幅值方差 (指数平滑)
    int   _stationaryTicks = 0;    // 连续静止 tick 计数
    float _biasAccum = 0;          // bias 累加器
    static constexpr float STATIONARY_VAR_THRESH = 0.02f; // (m/s²)² 方差阈值
    static constexpr int   STATIONARY_NEEDED = 20;        // 200ms×20 = 4秒静止

    // ── 校准状态 ──
    bool _calibrating = false;
    uint32_t _calibStartMs = 0;
    int _calibSampleCount = 0;
    uint32_t _calibDiagMs = 0;   // 进度诊断打印节流 (相对 _calibStartMs)

    // ── 校准累加器: 4 参数硬铁球面拟合 + 每轴 min/max (铺开度/进度显示) ──
    // 球面方程 (归一化坐标, ÷NORM 改善条件数): x²+y²+z² = a·x + b·y + c·z + d
    //   其中 a=2·ox, b=2·oy, c=2·oz, d=R²−|o|²; 每行 [x,y,z,1], 目标 x²+y²+z²。
    // 相比 min/max 取 offset=(min+max)/2: 球面拟合能从不完整的球面弧段估计球心,
    // 不会因某轴没转满而算出离谱的硬铁偏移。
    double _ata[10];   // 4×4 对称 AᵀA 上三角按行存储
    double _atb[4];    // Aᵀb
    int32_t _min[3] = { 32767,  32767,  32767 };
    int32_t _max[3] = { -32768, -32768, -32768 };
    static constexpr int32_t MIN_RANGE = 4000;   // 单轴极差阈值 (counts), 用于铺开度/进度

    // ── 内部方法 ──
    float computeTiltCompensatedHeading(float mx, float my, float mz,
                                        float ax, float ay, float az);
    void  computePitchRoll(float ax, float ay, float az);
    void  recordCalibSample(int16_t mx, int16_t my, int16_t mz);
    void  finishCalibration();
    void  calibReset();
    bool  solve4x4(double A[4][4], double b[4], double x[4]);   // 4×4 高斯消元(部分主元)
};

#endif
