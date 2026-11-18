// =====================================================================
//  game_coin.ino — coin flip (3rd in the menu)
//
//  Flick UP       : change mode: JUST FLIP, CALL HEADS, CALL TAILS
//  Hold SIDEWAYS  : toss the coin
//  Shake          : quit to the menu
//
//  JUST FLIP never ends: toss, see the side, toss again (it keeps a
//  heads / tails tally). With a call, a right call tosses again and the
//  score is your streak; the first wrong call ends the game.
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
#define COIN_RESULT_MS  1800
#define COIN_HELP_MS    2200
#define COIN_RING_R     80       // hold-to-toss progress dots
#define COIN_RING_DOTS  24
#define COIN_NUM_MODES  3

#define COIN_C_RIM      0x9362   // dark gold
#define COIN_C_FACE     0xF5E6   // gold
#define COIN_C_LIGHT    0xFEAB   // pale gold
#define COIN_C_EMBOSS   0xABC2   // raised design
#define COIN_C_EDGE     0x7AA1   // the coin's edge
#define COIN_C_GEM      0xE145   // red jewel on the crown

static const char* const COIN_MODE_NAME[COIN_NUM_MODES] = {"JUST FLIP", "CALL HEADS", "CALL TAILS"};

// Current face transform: design units -> canvas pixels (y squashed by coinSy)
static float coinCx, coinCy, coinU, coinSy;

static void coinEllipse(Adafruit_GFX& g, float cx, float cy, float rx, float ry, uint16_t c) {
  if (rx < 0.5f) return;
  if (ry < 0.5f) {
    g.drawFastHLine((int)lroundf(cx - rx), (int)lroundf(cy), (int)lroundf(2 * rx) + 1, c);
    return;
  }
  int y0 = (int)ceilf(cy - ry), y1 = (int)floorf(cy + ry);
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

// ---------------------------------------------------------------- text
static void coinTiny(const char* s, int cy, uint16_t col) {
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(col);
  int w = (int)strlen(s) * 6 - 1;
  tft.setCursor(120 - w / 2, cy - 4);
  tft.print(s);
}

static void coinTop(const char* s, uint16_t col) {
  tft.fillRect(40, 10, 160, 25, C_BLACK);
  drawCentered(s, 24, &FreeSansBold9pt7b, 1, col);
}

static void coinBottomClear() {
  tft.fillRect(50, 201, 140, 26, C_BLACK);
}

static void coinDrawMode(int mode, int streak, int nH, int nT) {
  coinTop(COIN_MODE_NAME[mode], mode == 0 ? COIN_C_FACE : C_WHITE);
  coinBottomClear();
  char b[24];
  if (mode == 0) snprintf(b, sizeof(b), "heads %d  tails %d", nH, nT);
  else           snprintf(b, sizeof(b), "wins %d", streak);
  coinTiny(b, 212, (mode != 0 && streak > 0) ? C_YELLOW : C_RINGDIM);
}

static void coinHoldRing(int dots, int &last) {
  if (dots == last) return;
  int from = last;
  if (dots < last) {
    for (int i = 0; i < COIN_RING_DOTS; i++) {
      float a = i * 2.0f * PI / COIN_RING_DOTS;
      tft.fillCircle(COIN_CX + (int)lroundf(COIN_RING_R * sinf(a)),
                     COIN_CY - (int)lroundf(COIN_RING_R * cosf(a)), 2, C_BLACK);
    }
    from = 0;
  }
  for (int i = from; i < dots; i++) {
    float a = i * 2.0f * PI / COIN_RING_DOTS;
    tft.fillCircle(COIN_CX + (int)lroundf(COIN_RING_R * sinf(a)),
                   COIN_CY - (int)lroundf(COIN_RING_R * cosf(a)), 2, C_YELLOW);
  }
  last = dots;
}

static void coinHelp() {
  tft.fillScreen(C_BLACK);
  drawMultiline("Coin flip", 72, 170, C_YELLOW);
  drawCentered("flick = call it", 120, &FreeSans9pt7b, 1, C_WHITE);
  drawCentered("hold sideways = flip", 146, &FreeSans9pt7b, 1, C_WHITE);
  drawCentered("right call = flip again", 180, &FreeSans9pt7b, 1, C_DIMTEXT);
  uint32_t t0 = millis();
  while (millis() - t0 < COIN_HELP_MS) {
    if (shakePoll()) return;
    delay(20);
  }
}

// ---------------------------------------------------------------- the toss
// Returns the side it lands on (0 heads, 1 tails), or -1 on a shake.
static int coinToss(GFXcanvas16* cv, uint8_t startFace) {
  uint8_t res = esp_random() & 1;
  int halves = 8 + (int)(esp_random() % 4);
  if (((startFace ^ (halves & 1)) & 1) != res) halves++;
  float endTheta = halves * PI;

  if (!cv) {                                     // no RAM for the canvas: no animation
    delay(600);
    return res;
  }

  uint32_t dur = COIN_FLIP_MS + esp_random() % 400;
  uint32_t t0 = millis();
  while (true) {
    float u = (millis() - t0) / (float)dur;
    if (u >= 1.0f) break;
    float e = 1.0f - powf(1.0f - u, 1.6f);      // spins fast, slows as it lands
    coinDrawFrame(*cv, endTheta * e, 1.0f + COIN_LIFT * sinf(PI * u), startFace);
    coinPush(cv);
    if (shakePoll()) return -1;
  }

  // land and wobble to rest
  float amp = 0.30f + (esp_random() % 100) / 1000.0f;
  t0 = millis();
  while (true) {
    uint32_t t = millis() - t0;
    if (t >= COIN_SETTLE_MS) break;
    float wob = amp * expf(-t / 130.0f) * sinf(2.0f * PI * t / 170.0f);
    coinDrawFrame(*cv, endTheta + wob, 1.0f, startFace);
    coinPush(cv);
    if (shakePoll()) return -1;
  }
  coinDrawFrame(*cv, endTheta, 1.0f, startFace);
  coinPush(cv);
  return res;
}

// =====================================================================
int playCoin() {
  if (DEBUG_USB) Serial.println("Coin flip start");

  static bool helpShown = false;               // once per wake
  static int mode = 0;                         // remembered between games
  static uint8_t face = 0;                     // side showing now
  static int nH = 0, nT = 0;                   // JUST FLIP tally (this wake)
  if (!helpShown) {
    helpShown = true;
    shakeReset();
    coinHelp();
    if (shakeSeen()) return 0;
  }

  tft.fillScreen(C_BLACK);
  GFXcanvas16* cv = new GFXcanvas16(COIN_CV, COIN_CV);
  if (cv && !cv->getBuffer()) { delete cv; cv = NULL; }
  if (cv) { coinDrawFrame(*cv, 0.0f, 1.0f, face); coinPush(cv); }

  int streak = 0;
  shakeReset();

  while (true) {
    // ---------------- choose mode ----------------
    coinDrawMode(mode, streak, nH, nT);
    int lastDots = 0;
    bool holding = false, needLevel = true, toss = false, quit = false;
    uint32_t holdStart = 0, lastAct = millis();

    while (!toss) {
      float up, right;
      tiltBoth(up, right);                     // also feeds shake-to-quit
      if (shakeSeen()) { quit = true; break; }

      if (menuFlick()) {                       // flick = next mode (wraps)
        mode = (mode + 1) % COIN_NUM_MODES;
        if (mode == 0) streak = 0;
        coinHoldRing(0, lastDots);
        coinDrawMode(mode, streak, nH, nT);
        holding = false;
        lastAct = millis();
      }

      // you arrive here still tilted from the menu: level out first
      if (fabsf(right) < MENU_PICK_DEG * 0.5f) needLevel = false;
      bool sideways = !needLevel && fabsf(right) > MENU_PICK_DEG;

      if (sideways) {
        if (!holding) { holding = true; holdStart = millis(); }
        float frac = (millis() - holdStart) / (float)MENU_HOLD_MS;
        if (frac >= 1.0f) toss = true;
        else coinHoldRing((int)(frac * COIN_RING_DOTS), lastDots);
        lastAct = millis();
      } else if (holding) {
        holding = false;
        coinHoldRing(0, lastDots);
      }

      if (millis() - lastAct > IDLE_ABORT_MS) {
        if (DEBUG_USB) Serial.println("Idle abort");
        quit = true;
        break;
      }
      delay(FRAME_MS);
    }
    if (quit) break;

    // ---------------- toss ----------------
    coinHoldRing(0, lastDots);
    coinBottomClear();
    int res = coinToss(cv, face);
    if (res < 0) break;                        // shaken mid-toss
    face = (uint8_t)res;
    if (res == 0) nH++; else nT++;

    bool win = (mode == 1 && res == 0) || (mode == 2 && res == 1);
    coinBottomClear();
    drawCentered(res == 0 ? "HEADS" : "TAILS", 213, &FreeSansBold12pt7b, 1, COIN_C_FACE);
    if (mode != 0) coinTop(win ? "RIGHT!" : "WRONG", win ? C_YELLOW : C_DANGER);
    if (DEBUG_USB) Serial.printf("Coin: %s%s\n", res == 0 ? "heads" : "tails",
                                 mode == 0 ? "" : win ? " -> right call" : " -> wrong call");

    uint32_t t1 = millis();
    while (millis() - t1 < COIN_RESULT_MS) {
      if (shakePoll()) break;
      delay(20);
    }
    if (shakeSeen()) break;
    if (mode == 0) continue;                   // JUST FLIP: toss again
    if (!win) break;
    streak++;
  }

  if (cv) delete cv;
  return streak;
}
