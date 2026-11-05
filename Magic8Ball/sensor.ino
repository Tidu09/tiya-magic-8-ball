// =====================================================================
//  sensor.ino — LIS3DH access, shake detection, tilt + calibration
// =====================================================================

bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(LIS_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(LIS_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)LIS_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

// 10-bit normal mode; accelScale is set by whichever config is active
bool readAccelG(float a[3]) {
  Wire.beginTransmission(LIS_ADDR);
  Wire.write(REG_OUT_X_L | 0x80);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)LIS_ADDR, (uint8_t)6) != 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();
  for (int k = 0; k < 3; k++) {
    a[k] = ((int16_t)((b[2 * k + 1] << 8) | b[2 * k]) >> 6) * accelScale;
  }
  return true;
}

// Waits briefly for a fresh sample, then reads
bool sampleAccel(float a[3]) {
  uint32_t t0 = millis();
  while (!(readReg(REG_STATUS) & 0x08)) {
    if (millis() - t0 > 30) break;
    delay(1);
  }
  return readAccelG(a);
}

// ---------------- Sensor modes ----------------
void configureWakeOnShake() {
  writeReg(REG_CTRL3, 0x00);
  writeReg(REG_INT1_CFG, 0x00);
  writeReg(REG_CLICK_CFG, 0x00);

  writeReg(REG_CTRL1, 0x4F);       // 50 Hz low-power XYZ
  writeReg(REG_CTRL2, 0x01);       // HPF on IA1 only
  writeReg(REG_CTRL4, 0x10);       // +-4 g
  writeReg(REG_CTRL5, 0x08);       // latch INT1
  writeReg(REG_CTRL6, 0x00);       // active-HIGH
  writeReg(REG_INT1_THS, WAKE_THS);
  writeReg(REG_INT1_DUR, WAKE_DUR);
  accelScale = 0.008f;

  readReg(REG_REFERENCE);
  delay(200);

  writeReg(REG_INT1_CFG, 0x2A);    // X-high | Y-high | Z-high
  writeReg(REG_CTRL3, 0x40);       // arm last
  readReg(REG_INT1_SRC);
}

// Gravity sensing for tilt: +-2 g, 100 Hz, no high-pass filter
void configureTiltMode() {
  writeReg(REG_CTRL3, 0x00);
  writeReg(REG_CTRL2, 0x00);
  writeReg(REG_CTRL1, 0x57);
  writeReg(REG_CTRL4, 0x00);
  accelScale = 0.004f;
  delay(15);
}

// Counts fast back-and-forth direction reversals on any axis
ShakeResult confirmShake() {
  writeReg(REG_CTRL3, 0x00);
  writeReg(REG_CTRL1, 0x57);       // 100 Hz normal mode
  writeReg(REG_CTRL4, 0x20);       // +-8 g
  writeReg(REG_CTRL2, 0x08);       // HP-filtered output
  accelScale = 0.016f;
  readReg(REG_REFERENCE);
  delay(15);

  ShakeResult r;
  r.peaks = 0;
  r.maxDev = 0.0f;

  int8_t lastSign[3] = {0, 0, 0};
  int run = 0;
  int skip = 3;
  uint32_t lastRev = 0;
  uint32_t start = millis();

  while (millis() - start < CONFIRM_MS && r.peaks < PEAKS_NEEDED) {
    if (!(readReg(REG_STATUS) & 0x08)) { delay(1); continue; }
    float a[3];
    if (!readAccelG(a)) continue;
    if (skip > 0) { skip--; continue; }

    float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (mag > r.maxDev) r.maxDev = mag;

    uint32_t now = millis();
    for (int k = 0; k < 3; k++) {
      int8_t s = 0;
      if (a[k] > REV_G) s = 1;
      else if (a[k] < -REV_G) s = -1;
      if (s == 0 || s == lastSign[k]) continue;

      bool reversal = (lastSign[k] != 0);
      lastSign[k] = s;
      if (!reversal) continue;

      if (run > 0 && now - lastRev < MIN_GAP_MS) continue;
      if (run > 0 && now - lastRev > MAX_GAP_MS) run = 0;
      run++;
      lastRev = now;
      if (run > r.peaks) r.peaks = run;
    }
  }

  writeReg(REG_CTRL2, 0x00);
  return r;
}

bool waitForStill(uint32_t timeoutMs) {
  uint32_t start = millis(), lowSince = 0;
  bool wasLow = false;
  while (millis() - start < timeoutMs) {
    readReg(REG_INT1_SRC);
    delay(5);
    if (digitalRead(WAKE_PIN) == LOW) {
      if (!wasLow) { wasLow = true; lowSince = millis(); }
      if (millis() - lowSince >= STILL_MS) return true;
    } else {
      wasLow = false;
    }
    delay(20);
  }
  return false;
}

// ---------------- Mode select ----------------
// Held edge-on during the greeting = the user wants the games
bool wantsGame(uint32_t windowMs) {
  uint32_t start = millis();
  int total = 0, edge = 0;
  while (millis() - start < windowMs) {
    float a[3];
    if (sampleAccel(a)) {
      float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
      if (mag > 0.6f && mag < 1.6f) {                 // ignore samples during motion
        if (millis() - start > windowMs / 3) {        // ignore the first third
          total++;
          if (fabsf(a[SCREEN_NORMAL_AXIS]) / mag < GAME_TILT_COS) edge++;
        }
      }
    }
    delay(15);
  }
  if (DEBUG_USB) Serial.printf("Mode sense: %d/%d edge-on\n", edge, total);
  return total >= 6 && edge > (total * 3) / 5;
}

// ---------------- Calibration ----------------
// 2 s countdown; only the last second is averaged into refVec
void calibrate() {
  tft.fillScreen(C_BLACK);
  drawMultiline("Hold still", 96, 170, C_WHITE);

  float sum[3] = {0, 0, 0};
  int n = 0;
  uint32_t start = millis();
  int lastRing = -1;

  while (millis() - start < CAL_TOTAL_MS) {
    uint32_t el = millis() - start;
    int ring = (el * 36) / CAL_TOTAL_MS;
    if (ring != lastRing) {
      float ang = ring * 2.0f * PI / 36 - PI / 2.0f;
      tft.fillCircle(120 + (int)(100 * cosf(ang)), 120 + (int)(100 * sinf(ang)), 3, C_YELLOW);
      lastRing = ring;
    }
    float a[3];
    if (sampleAccel(a) && el >= CAL_TOTAL_MS - CAL_USE_MS) {
      for (int k = 0; k < 3; k++) sum[k] += a[k];
      n++;
    }
    delay(10);
  }

  if (n < 5) { refVec[0] = 0; refVec[1] = 0; refVec[2] = 1; return; }
  float v[3];
  float mag = 0;
  for (int k = 0; k < 3; k++) { v[k] = sum[k] / n; mag += v[k] * v[k]; }
  mag = sqrtf(mag);
  if (mag < 0.3f) mag = 1.0f;
  for (int k = 0; k < 3; k++) refVec[k] = v[k] / mag;

  screenAngles(refVec, calUp, calRight);

  if (fabsf(refVec[SCREEN_UP_AXIS]) > CAL_BAD_DOT) {
    tft.fillScreen(C_BLACK);
    drawMultiline("Hold flatter", 120, 170, C_DANGER);
    delay(1200);
  }
  if (DEBUG_USB) Serial.printf("Ref: %.2f %.2f %.2f  calUp=%.1f calRight=%.1f\n",
                               refVec[0], refVec[1], refVec[2], calUp, calRight);
}

// ---------------- In-game shake detector ----------------
// On the +-2 g tilt range a real shake saturates the sensor, so a few
// over-threshold samples close together is a reliable "shake".
static uint32_t shkFirst = 0, shkLast = 0;
static int shkHits = 0;

static void shakeFeed(const float a[3]) {
  float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  uint32_t now = millis();
  if (shkHits > 0 && now - shkFirst > SHAKE_WIN_MS) shkHits = 0;
  if (mag > SHAKE_MAG && now - shkLast > SHAKE_MIN_GAP) {
    if (shkHits == 0) shkFirst = now;
    shkLast = now;
    shkHits++;
    if (shkHits >= SHAKE_HITS) gShakeSeen = true;
  }
}

void shakeReset() {
  shkHits = 0;
  shkFirst = millis();
  gShakeSeen = false;
}

bool shakeSeen() { return gShakeSeen; }

// For screens that are not reading tilt (e.g. game over)
bool shakePoll() {
  float a[3];
  if (readAccelG(a)) shakeFeed(a);
  return gShakeSeen;
}

// One sharp jerk along the screen-vertical axis = advance the menu.
// Edge-triggered: it must settle back near rest before the next one counts.
bool menuFlick() {
  static bool armed = true;
  static uint32_t lastFlick = 0;
  float a[3];
  if (!readAccelG(a)) return false;
  shakeFeed(a);

  float du = (a[SCREEN_UP_AXIS] - refVec[SCREEN_UP_AXIS]) * SCREEN_UP_SIGN;
  if (armed && fabsf(du) > MENU_FLICK_G && millis() - lastFlick > MENU_FLICK_GAP) {
    armed = false;
    lastFlick = millis();
    shakeReset();                 // <-- add this line only
    return true;
  }
  if (fabsf(du) < MENU_REARM_G) armed = true;
  return false;
}

// ---------------- Tilt ----------------
// Screen-frame pitch/roll via atan2 (stable at any hold angle, no saturation).
//   u > 0 : top of the screen tipped away from you
//   r > 0 : right edge tipped down
void screenAngles(const float a[3], float &u, float &r) {
  float n = fabsf(a[SCREEN_NORMAL_AXIS]);
  if (n < 0.02f) n = 0.02f;
  u = atan2f(a[SCREEN_UP_AXIS] * SCREEN_UP_SIGN, n) * 57.29578f;
  r = atan2f(a[SCREEN_RIGHT_AXIS] * SCREEN_RIGHT_SIGN, n) * 57.29578f;
}

// Degrees away from the calibrated hold, both axes from ONE i2c read
bool tiltBoth(float &up, float &right) {
  up = 0;
  right = 0;
  float a[3];
  if (!readAccelG(a)) return false;
  shakeFeed(a);
  float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  if (mag < 0.2f) return false;

  float u, r;
  screenAngles(a, u, r);
  up = u - calUp;
  right = r - calRight;
  return true;
}

float tiltUpDeg() {
  float u, r;
  tiltBoth(u, r);
  return u;
}
