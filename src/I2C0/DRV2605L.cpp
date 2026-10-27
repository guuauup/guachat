/**
 * DRV2605L LRA 驱动实现
 *
 * 关键参数沿用原 GUACHAT-2G4 的实测值：
 *   RATEDV 0x74 / CLAMPV 0x97，Library 6，LRA 周期 0x3C（约 169 Hz）。
 *   CONTROL1 bit4 必须为 1，否则 LRA 只会啸叫、不振。
 */
#include "DRV2605L.h"

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>

#include "I2C0/I2CBus.h"

namespace {

constexpr const char *NVS_NAMESPACE = "drv2605";

constexpr uint8_t DEFAULT_FEEDBACK  = 0xA8;  // LRA + brake factor 2 + loop gain 2
constexpr uint8_t DEFAULT_CONTROL1  = 0xF9;  // brake gain 7 | bit4=1 | blanking 2 | idiss 1
constexpr uint8_t DEFAULT_CONTROL2  = 0x60;  // 300 us BEMF sample + brake stabilizer
constexpr uint32_t CAL_TIMEOUT_MS   = 3000;
constexpr uint32_t PLAY_TIMEOUT_MS  = 3000;

}  // namespace

DRV2605LDriver::DRV2605LDriver(uint8_t addr)
  : _addr(addr)
  , _ready(false)
  , _standby(true)
  , _playing(false)
  , _deadline_ms(0)
{
}

// ============================================================
// 底层 I2C
// ============================================================
bool DRV2605LDriver::readReg(uint8_t reg, uint8_t &value)
{
  i2cBusLock();
  Wire.beginTransmission(_addr);
  Wire.write(reg);
  bool ok = (Wire.endTransmission(false) == 0);
  if (ok) {
    ok = (Wire.requestFrom(_addr, (uint8_t)1) == 1);
    if (ok) {
      value = Wire.read();
    }
  }
  if (ok) i2cBusReportSuccess(); else i2cBusReportError();
  i2cBusUnlock();
  return ok;
}

bool DRV2605LDriver::writeReg(uint8_t reg, uint8_t value)
{
  i2cBusLock();
  Wire.beginTransmission(_addr);
  Wire.write(reg);
  Wire.write(value);
  const bool ok = (Wire.endTransmission() == 0);
  if (ok) i2cBusReportSuccess(); else i2cBusReportError();
  i2cBusUnlock();
  return ok;
}

// ============================================================
// begin — 配置 + 校准 + standby
// ============================================================
bool DRV2605LDriver::begin()
{
  _ready = false;
  _standby = true;
  _playing = false;

  Serial.printf("[DRV2605L] Checking address 0x%02X on I2C0...\n", _addr);
  uint8_t status = 0;
  if (!readReg(REG_STATUS, status)) {
    Serial.println("[DRV2605L] ERROR: device not found");
    return false;
  }

  /* 配置和校准都在 active 模式下进行；结束后统一回 standby。 */
  if (!exitStandby()) {
    Serial.println("[DRV2605L] ERROR: failed to exit standby");
    return false;
  }

  if (!configure()) {
    Serial.println("[DRV2605L] ERROR: register configuration failed");
    enterIdle();
    return false;
  }

  if (loadCalibration()) {
    Serial.println("[DRV2605L] calibration restored from NVS");
  } else {
    calibrate();
  }

  if (!exitStandby()) {
    Serial.println("[DRV2605L] ERROR: failed to return to active mode");
    return false;
  }

  /* 实测该 LRA 谐振约 169 Hz；自动校准可能偏离，开机始终覆盖。 */
  writeReg(REG_LRARESON, BSP_HAPTIC_LRA_PERIOD);
  writeReg(REG_OVERDRIVE, 0);
  writeReg(REG_SUSTAINPOS, 0);
  writeReg(REG_SUSTAINNEG, 0);
  writeReg(REG_BREAK, 0);
  writeReg(REG_RATEDV, BSP_HAPTIC_IDLE_RV);
  enterIdle();

  _ready = true;
  Serial.printf("[DRV2605L] ready | V=%.2fV clamp=%.2fV f_res=%.0fHz | standby\n",
                BSP_HAPTIC_RATED_RV * 0.0216f,
                BSP_HAPTIC_CLAMP_RV * 0.0216f,
                1000000.0f / (BSP_HAPTIC_LRA_PERIOD * 98.46f));
  return true;
}

// ============================================================
// 控制参数
// ============================================================
bool DRV2605LDriver::configure()
{
  bool ok = true;

  /* FEEDBACK：N_ERM_LRA=1，brake factor=2，loop gain=2 */
  ok = writeReg(REG_FEEDBACK, DEFAULT_FEEDBACK) && ok;

  /* CONTROL3：清除 ERM_OPEN_LOOP，LRA 必须闭环 */
  uint8_t ctrl3 = 0;
  if (readReg(REG_CONTROL3, ctrl3)) {
    ok = writeReg(REG_CONTROL3, (uint8_t)(ctrl3 & ~0x20)) && ok;
  } else {
    ok = false;
  }

  /* CONTROL1：AUTO_BRAKE_GAIN=7，bit4 必须为 1 */
  ok = writeReg(REG_CONTROL1, DEFAULT_CONTROL1) && ok;
  ok = writeReg(REG_CONTROL2, DEFAULT_CONTROL2) && ok;

  /* CONTROL4：LRA_DRIVE_TIME=0（1x） */
  uint8_t ctrl4 = 0;
  if (readReg(REG_CONTROL4, ctrl4)) {
    const uint8_t value = (uint8_t)(ctrl4 & ~(0x03 << 2));
    ok = writeReg(REG_CONTROL4, value) && ok;
  } else {
    ok = false;
  }

  ok = writeReg(REG_RATEDV, BSP_HAPTIC_RATED_RV) && ok;
  ok = writeReg(REG_CLAMPV, BSP_HAPTIC_CLAMP_RV) && ok;
  ok = writeReg(REG_LIBRARY, 6) && ok;  // Library 6 = LRA
  return ok;
}

bool DRV2605LDriver::loadCalibration()
{
  Preferences prefs;
  uint8_t comp = 0;
  uint8_t emp = 0;
  uint8_t feedback = 0;
  bool done = false;

  if (prefs.begin(NVS_NAMESPACE, true)) {
    done = prefs.getBool("done", false);
    if (done) {
      comp = prefs.getUChar("comp", 0);
      emp = prefs.getUChar("emp", 0);
      feedback = prefs.getUChar("fb", 0);
    }
    prefs.end();
  }

  if (!done || (feedback & 0x80) == 0) {
    return false;
  }

  writeReg(REG_AUTOCALCOMP, comp);
  writeReg(REG_AUTOCALEMP, emp);
  writeReg(REG_FEEDBACK, feedback);
  return true;
}

bool DRV2605LDriver::calibrate()
{
  uint8_t status = 0;
  uint8_t comp = 0;
  uint8_t emp = 0;
  uint8_t period = 0;
  uint8_t feedback = 0;

  if (!runCalibration(status, comp, emp, period, feedback)) {
    return false;
  }

  saveCalibration(comp, emp, period, feedback);
  return (status & 0x08) != 0;
}

bool DRV2605LDriver::runCalibration(uint8_t &status, uint8_t &comp,
                                    uint8_t &emp, uint8_t &period,
                                    uint8_t &feedback)
{
  /* 校准期间关闭 overdrive/brake，避免干扰反电动势测量 */
  writeReg(REG_OVERDRIVE, 0);
  writeReg(REG_SUSTAINPOS, 0);
  writeReg(REG_SUSTAINNEG, 0);
  writeReg(REG_BREAK, 0);

  Serial.print("[DRV2605L] LRA auto-calibration...");
  if (!writeReg(REG_MODE, MODE_AUTOCAL) || !writeReg(REG_GO, 1)) {
    Serial.println(" failed to start");
    return false;
  }

  const uint32_t start = millis();
  uint8_t go = 1;
  while (millis() - start < CAL_TIMEOUT_MS) {
    delay(10);
    if (!readReg(REG_GO, go) || go == 0) {
      break;
    }
  }

  if (go != 0) {
    Serial.println(" timeout");
    return false;
  }

  status = 0;
  comp = 0;
  emp = 0;
  period = 0;
  feedback = 0;
  readReg(REG_STATUS, status);
  readReg(REG_AUTOCALCOMP, comp);
  readReg(REG_AUTOCALEMP, emp);
  readReg(REG_LRARESON, period);
  readReg(REG_FEEDBACK, feedback);

  const bool diag_ok = (status & 0x08) != 0;
  const bool period_ok = (period > 0 && period < 0xFF);
  const float freq_hz = period_ok ? 1000000.0f / (period * 98.46f) : 0.0f;

  Serial.printf(" %s | status=0x%02X comp=0x%02X emp=0x%02X period=0x%02X (~%.0fHz)\n",
                diag_ok ? "OK" : "WARN", status, comp, emp, period, freq_hz);
  if (!period_ok) {
    Serial.println("[DRV2605L] WARN: invalid LRA period, using manual value");
  }
  return true;
}

bool DRV2605LDriver::saveCalibration(uint8_t comp, uint8_t emp,
                                     uint8_t period, uint8_t feedback)
{
  if (period == 0 || period == 0xFF || feedback == 0) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    return false;
  }
  prefs.putBool("done", true);
  prefs.putUChar("comp", comp);
  prefs.putUChar("emp", emp);
  prefs.putUChar("fb", feedback);
  prefs.end();
  Serial.println("[DRV2605L] calibration saved to NVS");
  return true;
}

// ============================================================
// recalibrate — 手动重新校准 LRA（阻塞 1–3 秒）
// ============================================================
bool DRV2605LDriver::recalibrate()
{
  if (!_ready) {
    Serial.println("[DRV2605L] recalibrate: driver not ready");
    return false;
  }

  _playing = false;
  _deadline_ms = 0;
  if (!exitStandby()) {
    return false;
  }

  uint8_t savedOD = 0;
  uint8_t savedSP = 0;
  uint8_t savedSN = 0;
  uint8_t savedBrake = 0;
  if (!readReg(REG_OVERDRIVE, savedOD) ||
      !readReg(REG_SUSTAINPOS, savedSP) ||
      !readReg(REG_SUSTAINNEG, savedSN) ||
      !readReg(REG_BREAK, savedBrake)) {
    enterIdle();
    return false;
  }

  /* 校准条件必须和 begin() 完全一致：反馈增益清零、LRA 闭环、
     RATEDV/CLAMPV 回到额定值。上一次播放留下的低压会让测量偏弱。 */
  if (!configure()) {
    Serial.println("[DRV2605L] recalibrate: configuration failed");
    writeReg(REG_OVERDRIVE, savedOD);
    writeReg(REG_SUSTAINPOS, savedSP);
    writeReg(REG_SUSTAINNEG, savedSN);
    writeReg(REG_BREAK, savedBrake);
    enterIdle();
    return false;
  }

  uint8_t status = 0;
  uint8_t comp = 0;
  uint8_t emp = 0;
  uint8_t period = 0;
  uint8_t feedback = 0;
  const bool completed = runCalibration(status, comp, emp, period, feedback);

  /* 无论成功与否都把 overdrive/brake 恢复；这些值不参与校准结果。 */
  writeReg(REG_OVERDRIVE, savedOD);
  writeReg(REG_SUSTAINPOS, savedSP);
  writeReg(REG_SUSTAINNEG, savedSN);
  writeReg(REG_BREAK, savedBrake);

  if (!completed) {
    enterIdle();
    return false;
  }

  saveCalibration(comp, emp, period, feedback);

  /* 实测此 LRA 约 169 Hz，自动校准可能偏离；播放始终使用固定周期。 */
  writeReg(REG_LRARESON, BSP_HAPTIC_LRA_PERIOD);
  enterIdle();
  return (status & 0x08) != 0;
}

// ============================================================
// 播放与空闲管理
// ============================================================
bool DRV2605LDriver::exitStandby()
{
  if (!_standby) {
    return true;
  }

  if (!writeReg(REG_MODE, MODE_INTTRIG)) {
    return false;
  }

  _standby = false;
  delay(1);  // 等待内部振荡器稳定
  return true;
}

void DRV2605LDriver::enterIdle()
{
  writeReg(REG_RATEDV, BSP_HAPTIC_IDLE_RV);
  if (writeReg(REG_MODE, MODE_STANDBY)) {
    _standby = true;
  }
  _playing = false;
  _deadline_ms = 0;
}

bool DRV2605LDriver::play(uint8_t effect, uint8_t rated_reg)
{
  if (!_ready || effect == 0) {
    return false;
  }

  bool ok = writeReg(REG_RATEDV, rated_reg) && exitStandby();
  if (ok) {
    ok = writeReg(REG_WAVESEQ1, effect) &&
         writeReg(REG_WAVESEQ1 + 1, 0) &&
         writeReg(REG_MODE, MODE_INTTRIG) &&
         writeReg(REG_GO, 1);
  }

  if (!ok) {
    enterIdle();
    return false;
  }

  _playing = true;
  _deadline_ms = millis() + PLAY_TIMEOUT_MS;
  return true;
}

bool DRV2605LDriver::poll()
{
  if (!_ready || !_playing) {
    return false;
  }

  if ((int32_t)(millis() - _deadline_ms) >= 0) {
    Serial.println("[DRV2605L] WARN: playback timeout, forcing standby");
    enterIdle();
    return true;
  }

  uint8_t go = 0;
  if (!readReg(REG_GO, go)) {
    return false;
  }

  if (go == 0) {
    enterIdle();
    return true;
  }

  return false;
}

void DRV2605LDriver::stop()
{
  if (_ready) {
    enterIdle();
  }
}
