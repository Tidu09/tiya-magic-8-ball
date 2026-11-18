// =====================================================================
//  game_coin.ino — coin flip (3rd in the menu)
//
//  Flick UP  : toss the coin, any time, even while it is still spinning
//              (own soft detector, coinFlick: tune with COIN_FLICK_G)
//  Shake     : back to the menu
//  No modes, no score, no text. 60 s with no flick = back to the menu.
//
//  Heads = a crown, tails = a star. The coin is drawn into a small
//  off-screen canvas and pushed in one go, so the toss never flickers.
//  It spins as an ellipse squashed by |cos(angle)|, shows its edge as it
//  turns, rises towards you (gets bigger) mid-toss, then wobbles to rest.
//  The result is chosen first and the number of half-turns is picked so
//  the coin lands on it: exactly 50 / 50.
//
//  Every name here is prefixed COIN_ / coin because Arduino glues the
//  tabs together.
// =====================================================================

#define COIN_CV         148      // canvas size (square), centred on the coin
#define COIN_CX         120      // coin centre on the screen
#define COIN_CY         118
#define COIN_R          52.0f    // resting radius
#define COIN_LIFT       0.25f    // extra size at the top of the toss
#define COIN_THICK      7.0f     // edge thickness seen side-on
#define COIN_FLIP_MS    1500     // toss time (+ up to 400 ms random)
#define COIN_SETTLE_MS  480      // wobble after landing
#define COIN_IDLE_MS    60000UL  // no flick for this long = back to the menu
#define COIN_FLICK_G     0.28f   // up-jerk this hard (g off rest) to toss. LOWER = more sensitive
#define COIN_FLICK_REARM 0.14f   // must settle back under this before the next toss
#define COIN_FLICK_GAP   110     // ms lockout after a toss, swallows the rebound

#define COIN_C_RIM      0x9362   // dark gold
#define COIN_C_FACE     0xF5E6   // gold
#define COIN_C_LIGHT    0xFEAB   // pale gold
#define COIN_C_EMBOSS   0xABC2   // raised design
#define COIN_C_EDGE     0x7AA1   // the coin's edge
#define COIN_C_GEM      0xE145   // red jewel on the crown

// Current face transform: design units -> canvas pixels (y squashed by coinSy)
static float coinCx, coinCy, coinU, coinSy;

static void coinEllipse(Adafruit_GFX& g, float cx, float cy, float rx, float ry, uint16_t c) {
  // Guard: a NaN/inf here turns into a ~4-billion-row loop on the ESP32-C3
  // (float->int of NaN gives INT_MAX), which looks exactly like a frozen screen.
  if (!isfinite(cx) || !isfinite(cy) || !isfinite(rx) || !isfinite(ry)) return;
  if (rx < 0.5f) return;
  if (ry < 0.5f) {
    g.drawFastHLine((int)lroundf(cx - rx), (int)lroundf(cy), (int)lroundf(2 * rx) + 1, c);
    return;
  }
  float fy0 = fmaxf(ceilf(cy - ry), 0.0f), fy1 = fminf(floorf(cy + ry), (float)(COIN_CV - 1));
  if (fy1 < fy0) return;                         // off the canvas
  int y0 = (int)fy0, y1 = (int)fy1;              // rows clamped to the canvas: always ends
  for (int y = y0; y <= y1; y++) {
    float k = (y - cy) / ry;
    float hw = rx * sqrtf(fmaxf(0.0f, 1.0f - k * k));
    int x0 = (int)lroundf(cx - hw), x1 = (int)lroundf(cx + hw);
    g.drawFastHLine(x0, y, x1 - x0 + 1, c);
  }
}

static int coinPX(float x) { return (int)lroundf(coinCx + x * coinU); }
static int coinPY(float y) { return (int)lroundf(coinCy + y * coinU * coinSy); }

static void coinTri(Adafruit_GFX& g, float x0, float y0, float x1, float y1,
                    float x2, float y2, uint16_t c) {
  g.fillTriangle(coinPX(x0), coinPY(y0), coinPX(x1), coinPY(y1), coinPX(x2), coinPY(y2), c);
}

static void coinRect(Adafruit_GFX& g, float x0, float y0, float x1, float y1, uint16_t c) {
  int a = coinPX(x0), b = coinPY(y0);
  g.fillRect(a, b, coinPX(x1) - a + 1, coinPY(y1) - b + 1, c);
}

static void coinDot(Adafruit_GFX& g, float x, float y, float r, uint16_t c) {
  coinEllipse(g, coinCx + x * coinU, coinCy + y * coinU * coinSy, r * coinU, r * coinU * coinSy, c);
}

// Heads: a crown with three jewelled points and a jewelled band
static void coinDrawCrown(Adafruit_GFX& g) {
  coinRect(g, -0.52f, -0.06f, 0.52f, 0.15f, COIN_C_EMBOSS);              // body
  coinTri(g, -0.52f, -0.05f, -0.58f, -0.40f, -0.22f, -0.05f, COIN_C_EMBOSS);
  coinTri(g, -0.24f, -0.05f,  0.00f, -0.52f,  0.24f, -0.05f, COIN_C_EMBOSS);
  coinTri(g,  0.22f, -0.05f,  0.58f, -0.40f,  0.52f, -0.05f, COIN_C_EMBOSS);
  coinDot(g, -0.58f, -0.44f, 0.075f, COIN_C_EMBOSS);                     // balls on the points
  coinDot(g,  0.00f, -0.57f, 0.080f, COIN_C_EMBOSS);
  coinDot(g,  0.58f, -0.44f, 0.075f, COIN_C_EMBOSS);
  coinRect(g, -0.54f, 0.19f, 0.54f, 0.40f, COIN_C_EMBOSS);               // band
  coinDot(g,  0.00f, 0.295f, 0.070f, COIN_C_GEM);                        // jewels
  coinDot(g, -0.32f, 0.295f, 0.045f, COIN_C_LIGHT);
  coinDot(g,  0.32f, 0.295f, 0.045f, COIN_C_LIGHT);
  coinDot(g,  0.00f, -0.57f, 0.030f, COIN_C_LIGHT);
}

// Tails: a five-point star with three sparkles
static void coinDrawStar(Adafruit_GFX& g) {
  const float sx = 0.0f, sy = 0.04f;
  float px[10], py[10];
  for (int k = 0; k < 10; k++) {
    float a = -PI / 2 + k * PI / 5;
    float r = (k & 1) ? 0.25f : 0.62f;
    px[k] = sx + r * cosf(a);
    py[k] = sy + r * sinf(a);
  }
  for (int k = 0; k < 10; k++) {
    int n = (k + 1) % 10;
    coinTri(g, sx, sy, px[k], py[k], px[n], py[n], COIN_C_EMBOSS);
  }
  coinDot(g, sx, sy, 0.07f, COIN_C_LIGHT);
  coinDot(g,  0.52f, -0.45f, 0.050f, COIN_C_EMBOSS);
  coinDot(g, -0.62f,  0.22f, 0.042f, COIN_C_EMBOSS);
  coinDot(g,  0.60f,  0.25f, 0.035f, COIN_C_EMBOSS);
}

// One frame: angle theta (0 = startFace up, each half-turn flips the side)
static void coinDrawFrame(Adafruit_GFX& g, float theta, float scale, uint8_t startFace) {
  if (!isfinite(theta)) theta = 0.0f;            // never draw from a bad number
  if (!isfinite(scale) || scale < 0.5f || scale > 1.6f) scale = 1.0f;
  g.fillScreen(C_BLACK);
  float c = cosf(theta), s = sinf(theta);
  float sy = fabsf(c);
  float R = COIN_R * scale;
  float th = COIN_THICK * scale * fabsf(s);
  float cx = COIN_CV / 2.0f;
  float dir = (c * s >= 0) ? 1.0f : -1.0f;      // which side the edge shows on
  float faceCy = COIN_CV / 2.0f - dir * th / 2;
  float backCy = faceCy + dir * th;

  if (th >= 0.5f) {                              // the edge
    coinEllipse(g, cx, backCy, R, R * sy, COIN_C_EDGE);
    int y0 = (int)lroundf(fminf(faceCy, backCy)), y1 = (int)lroundf(fmaxf(faceCy, backCy));
    g.fillRect((int)lroundf(cx - R), y0, (int)lroundf(2 * R) + 1, y1 - y0 + 1, COIN_C_EDGE);
  }
  if (sy < 0.05f) return;                        // seen edge-on: nothing else

  uint8_t face = startFace ^ (uint8_t)((int)floorf((theta + PI / 2) / PI) & 1);
  coinEllipse(g, cx, faceCy, R, R * sy, COIN_C_RIM);
  for (int i = 0; i < 32; i++) {                 // milled rim
    float a = i * 2.0f * PI / 32;
    coinEllipse(g, cx + 0.94f * R * cosf(a), faceCy + 0.94f * R * sinf(a) * sy,
                1.1f * scale, 1.1f * scale * sy, COIN_C_FACE);
  }
  coinEllipse(g, cx, faceCy, R * 0.86f, R * 0.86f * sy, COIN_C_FACE);
  coinEllipse(g, cx, faceCy, R * 0.79f, R * 0.79f * sy, COIN_C_EMBOSS);  // thin inner ring
  coinEllipse(g, cx, faceCy, R * 0.76f, R * 0.76f * sy, COIN_C_FACE);

  coinCx = cx; coinCy = faceCy; coinU = R * 0.95f; coinSy = sy;
  if (face == 0) coinDrawCrown(g);
  else           coinDrawStar(g);
}

static void coinPush(GFXcanvas16* cv) {
  tft.drawRGBBitmap(COIN_CX - COIN_CV / 2, COIN_CY - COIN_CV / 2, cv->getBuffer(), COIN_CV, COIN_CV);
}

// ------------------------------------------------------------ flick gesture
// Own detector, like the platformer's jump (pfJump). It never touches the
// shake counter, so a shake always gets through. Do NOT use menuFlick() in
// a game: it resets the shake counter on every jerk, which swallows shakes.
static bool coinFlick() {
  static bool armed = true;
  static uint32_t lastFlick = 0;
  float a[3];
  if (!readAccelG(a)) return false;

  float du = (a[SCREEN_UP_AXIS] - refVec[SCREEN_UP_AXIS]) * SCREEN_UP_SIGN;
  if (armed && du > COIN_FLICK_G && millis() - lastFlick > COIN_FLICK_GAP) {
    armed = false;
    lastFlick = millis();
    return true;
  }
  if (fabsf(du) < COIN_FLICK_REARM) armed = true;
  return false;
}

// One frame of input, the same as every other game: tiltBoth() feeds the
// shake-to-quit detector, then the game's own gesture detector.
static bool coinInput() {
  float up, right;
  tiltBoth(up, right);
  return coinFlick();
}

// ---------------------------------------------------------------- the toss
// Tosses from `face` lying flat. A flick at ANY moment (mid-air or while it
// wobbles) tosses it again from wherever it is, so it never feels stuck.
// Returns the side it lands on (0 heads, 1 tails), or -1 on a shake.
// The result is picked first and the number of half-turns is chosen so the
// coin lands on it: exactly 50 / 50.
static int coinToss(GFXcanvas16* cv, uint8_t face) {
  float theta = 0.0f, scale = 1.0f;
  while (true) {                                   // one pass per flick
    uint8_t res = esp_random() & 1;
    int n = (int)floorf(theta / PI) + 8 + (int)(esp_random() % 4);
    if (((face ^ (n & 1)) & 1) != res) n++;
    float th0 = theta, th1 = n * PI, s0 = scale;
    bool again = false;

    uint32_t dur = COIN_FLIP_MS + esp_random() % 400;
    uint32_t t0 = millis();
    while (true) {
      float u = (millis() - t0) / (float)dur;
      if (u >= 1.0f) break;
      float e = 1.0f - powf(1.0f - u, 1.6f);       // spins fast, slows as it lands
      theta = th0 + (th1 - th0) * e;
      scale = 1.0f + COIN_LIFT * sinf(PI * u) + (s0 - 1.0f) * (1.0f - u);
      coinDrawFrame(*cv, theta, scale, face);
      coinPush(cv);
      if (coinInput()) { again = true; break; }    // flicked mid-air: toss again
      if (shakeSeen()) return -1;
    }
    if (again) continue;

    // land and wobble to rest
    scale = 1.0f;
    float amp = 0.30f + (esp_random() % 100) / 1000.0f;
    t0 = millis();
    while (true) {
      uint32_t t = millis() - t0;
      if (t >= COIN_SETTLE_MS) break;
      // (float) BEFORE the minus: t is unsigned, so -t would be ~4 billion,
      // expf() = infinity, and the angle would become NaN (this froze v10-v13).
      float tf = (float)t;
      theta = th1 + amp * expf(-tf / 130.0f) * sinf(2.0f * PI * tf / 170.0f);
      coinDrawFrame(*cv, theta, 1.0f, face);
      coinPush(cv);
      if (coinInput()) { again = true; break; }
      if (shakeSeen()) return -1;
    }
    if (again) continue;

    coinDrawFrame(*cv, th1, 1.0f, face);           // flat: same picture as res at angle 0
    coinPush(cv);
    return res;
  }
}

// =====================================================================
// Flick = toss. Again and again until a shake (back to the menu).
// No modes, no score, no text: crown up = heads, star up = tails.
void playCoin() {
  if (DEBUG_USB) Serial.println("Coin flip start");
  static uint8_t face = 0;                     // side showing now (kept between visits)

  tft.fillScreen(C_BLACK);
  GFXcanvas16* cv = new GFXcanvas16(COIN_CV, COIN_CV);
  if (cv && !cv->getBuffer()) { delete cv; cv = NULL; }
  if (!cv) {
    drawMultiline("No memory\nfor the coin", 120, 170, C_DANGER);
    delay(2000);
    return;
  }
  coinDrawFrame(*cv, 0.0f, 1.0f, face);
  coinPush(cv);

  shakeReset();
  uint32_t lastAct = millis();
  while (true) {
    if (coinInput()) {                         // a little flick up = toss
      int res = coinToss(cv, face);
      if (res < 0) break;                      // shaken mid-toss
      face = (uint8_t)res;
      if (DEBUG_USB) Serial.printf("Coin: %s\n", res == 0 ? "heads" : "tails");
      shakeReset();
      lastAct = millis();
    }
    if (shakeSeen()) break;
    if (millis() - lastAct > COIN_IDLE_MS) break;   // battery guard: back to the menu
    delay(FRAME_MS);
  }
  delete cv;
}
