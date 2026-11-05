// =====================================================================
//  game_roulette.ino — European roulette (single zero, 0-36)
//
//  Flick UP       : change mode / bet
//                   NORMAL (just spin and see the number), then
//                   RED / BLACK / ODD / EVEN / 1-18 / 19-36
//  Hold SIDEWAYS  : spin the ball
//  Shake          : quit to the menu
//
//  NORMAL never ends: spin, see the number, spin again (handy for
//  picking people). Every real bet pays even money, so the score is your
//  winning streak: a win spins again, the first loss ends the game.
//  0 (green) loses every bet, exactly like the real table.
//
//  The finish is built for suspense: once the ball reaches the pockets,
//  each fret is a little hill it has to climb. Fast, it clatters over
//  them; slow, it hesitates on top of one ("will it go over?") and then
//  rocks to rest. The centre shows the number under the ball live, and
//  the final pocket flashes before the result.
//
//  The wheel is painted pixel by pixel from ONE colour function (rouBg).
//  When the ball moves it asks that same function what was underneath,
//  so it can roll across pockets, frets, numbers and diamonds without
//  ever leaving a mark. The spinning turret in the middle is drawn into
//  a small off-screen canvas and pushed in one go, so it never flickers.
//
//  Every #define here is prefixed ROU_ because Arduino glues the tabs
//  together and these names are visible in every tab after this one.
// =====================================================================

// ---- wheel geometry: radii in px from the screen centre ----
#define ROU_R_SCREEN    119.5f
#define ROU_R_RIM       110.0f   // wooden rim outside this (gold lip first)
#define ROU_R_LIP       111.3f
#define ROU_R_TRACK_IN  99.0f    // ball track 99..110
#define ROU_R_APRON_OUT 97.8f    // gold line 97.8..99
#define ROU_R_APRON_IN  88.0f    // sloped apron with the 8 diamonds 88..97.8
#define ROU_R_NUM_OUT   87.0f    // gold line 87..88
#define ROU_R_NUM_IN    74.0f    // coloured number band 74..87
#define ROU_R_POCK_IN   60.0f    // pockets (darker) 60..74
#define ROU_R_CONE      56.0f    // gold ring 56..60
#define ROU_R_HUB       44.0f    // plain hub inside this (turret + panels)

#define ROU_LABEL_R     80.5f
#define ROU_DIAMOND_R   93.0f
#define ROU_DIAMOND_SZ  7        // diamond half-size, in half-pixels
#define ROU_FRET_PX     0.8f     // fret half-thickness

#define ROU_BALL_R      4
#define ROU_BALL_TRACK  104.5f   // orbit radius on the track
#define ROU_BALL_POCK   67.0f    // resting radius in a pocket
#define ROU_PANEL_R     38       // centre panel (bet / result)
#define ROU_RING_R      41       // hold-to-spin progress dots
#define ROU_RING_DOTS   24
#define ROU_TURRET      52       // turret canvas size (square, fits inside the hub)

// ---- ball feel ----
#define ROU_W_MIN       0.30f    // launch speed, radians per frame
#define ROU_W_MAX       0.42f
#define ROU_W_DECAY     0.988f   // slowdown on the track
#define ROU_W_DROP      0.11f    // falls off the track below this speed
#define ROU_FALL_G      0.12f    // px per frame^2 down the apron
#define ROU_HOP0        5.0f     // first bounce height in the pockets
#define ROU_HOP_DAMP    0.55f
#define ROU_FRET_HILL   0.00045f // how hard frets push the ball to a pocket centre
#define ROU_POCK_DAMP   0.970f   // rolling friction in the pockets (higher = longer finish)
#define ROU_W_STOP      0.0006f  // at rest below this...
#define ROU_CENTRE_TOL  0.06f    // ...and this close to a pocket centre (in pockets)
#define ROU_POCK_MAX_MS 9000UL   // safety cap on the finish
#define ROU_BLINKS      3        // winning pocket flashes before the result
#define ROU_WHEEL_W     0.10f    // turret speed, radians per frame
#define ROU_SPIN_MAX_MS 12000UL
#define ROU_RESULT_MS   1700
#define ROU_HELP_MS     2200

// ---- colours ----
#define ROU_C_WOOD      0x69E3
#define ROU_C_TRACK     0x38E1
#define ROU_C_APRON     0x28A1
#define ROU_C_GOLD      0xDD24
#define ROU_C_SHINE     0xFF7A
#define ROU_C_FRET      0xC618
#define ROU_C_RED       0xC800
#define ROU_C_RED_D     0x8000
#define ROU_C_BLK       0x2104
#define ROU_C_BLK_D     0x0841
#define ROU_C_GRN       0x0420
#define ROU_C_GRN_D     0x0260
#define ROU_C_CONE      0x59A3
#define ROU_C_HUB       0x4142
#define ROU_C_PANEL     0x10A2
#define ROU_C_BALL_EDGE 0xBDF7
#define ROU_C_BET_RED   0xF8A2
#define ROU_C_RED_HI    0xFA69   // flashing pocket
#define ROU_C_BLK_HI    0x8410
#define ROU_C_GRN_HI    0x07E8

#define ROU_SLOT        (2.0f * PI / 37.0f)
#define ROU_SQ(r)       ((int32_t)((r) * 2.0f * (r) * 2.0f))   // radius -> (2r)^2

// Real European wheel order, clockwise from the zero
static const uint8_t ROU_WHEEL[37] = {
  0, 32, 15, 19, 4, 21, 2, 25, 17, 34, 6, 27, 13, 36, 11, 30, 8, 23, 10,
  5, 24, 16, 33, 1, 20, 14, 31, 9, 22, 18, 29, 7, 28, 12, 35, 3, 26
};

#define ROU_NUM_BETS 7                       // index 0 is NORMAL (no bet)
static const char* const ROU_BET_NAME[ROU_NUM_BETS] = {
  "NORMAL", "RED", "BLACK", "ODD", "EVEN", "1-18", "19-36"
};

static int16_t rouDiaX2[8], rouDiaY2[8];          // diamond centres, half-pixel units
static int16_t rouLx[37], rouLy[37], rouLw[37];   // number label boxes
static bool rouReady = false;
static int rouHl = -1;                            // flashing pocket slot, -1 = none

// ---------------------------------------------------------------- rules
static bool rouIsRed(int n) {
  if (n <= 0) return false;
  bool odd = (n & 1);
  if ((n >= 1 && n <= 10) || (n >= 19 && n <= 28)) return odd;
  return !odd;
}

static uint16_t rouPocketHi(int n) {
  if (n == 0)      return ROU_C_GRN_HI;
  return rouIsRed(n) ? ROU_C_RED_HI : ROU_C_BLK_HI;
}

static uint16_t rouPocketCol(int n, bool deep) {
  if (n == 0)      return deep ? ROU_C_GRN_D : ROU_C_GRN;
  if (rouIsRed(n)) return deep ? ROU_C_RED_D : ROU_C_RED;
  return deep ? ROU_C_BLK_D : ROU_C_BLK;
}

static bool rouWins(int bet, int n) {
  if (n == 0) return false;                 // the house edge
  switch (bet) {
    case 1:  return rouIsRed(n);
    case 2:  return !rouIsRed(n);
    case 3:  return (n & 1);
    case 4:  return !(n & 1);
    case 5:  return n <= 18;
    case 6:  return n >= 19;
    default: return false;                  // NORMAL has no bet
  }
}

// ---------------------------------------------------------------- geometry
// Fast atan2 (max error ~1e-5 rad), the C3 has no FPU
static float rouAtan2(float y, float x) {
  float ax = fabsf(x), ay = fabsf(y);
  float mx = (ax > ay) ? ax : ay;
  float mn = (ax > ay) ? ay : ax;
  if (mx < 1e-6f) return 0.0f;
  float a = mn / mx;
  float s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0)   r = 3.14159274f - r;
  if (y < 0)   r = -r;
  return r;
}

// Angle clockwise from 12 o'clock, 0..2pi
static float rouWrap(float th) {
  while (th >= 2.0f * PI) th -= 2.0f * PI;
  while (th < 0.0f)       th += 2.0f * PI;
  return th;
}

static int rouSlotAt(float th) {
  int s = (int)lroundf(th / ROU_SLOT);
  return ((s % 37) + 37) % 37;
}

static void rouInit() {
  if (rouReady) return;
  for (int k = 0; k < 8; k++) {
    float th = k * PI / 4.0f;
    rouDiaX2[k] = (int16_t)lroundf(2.0f * ROU_DIAMOND_R * sinf(th));
    rouDiaY2[k] = (int16_t)lroundf(-2.0f * ROU_DIAMOND_R * cosf(th));
  }
  for (int s = 0; s < 37; s++) {
    float th = s * ROU_SLOT;
    float cx = ROU_R_SCREEN + ROU_LABEL_R * sinf(th);
    float cy = ROU_R_SCREEN - ROU_LABEL_R * cosf(th);
    rouLw[s] = (ROU_WHEEL[s] >= 10) ? 11 : 5;
    rouLx[s] = (int16_t)lroundf(cx - rouLw[s] / 2.0f);
    rouLy[s] = (int16_t)lroundf(cy - 3.5f);
  }
  rouReady = true;
}

// The whole wheel as a colour function of one pixel
static uint16_t rouBg(int x, int y) {
  int32_t X = 2 * x - 239, Y = 2 * y - 239;       // centre = 119.5, half-pixel units
  int32_t D = X * X + Y * Y;

  if (D >= ROU_SQ(ROU_R_SCREEN))    return C_BLACK;
  if (D >= ROU_SQ(ROU_R_LIP))       return ROU_C_WOOD;
  if (D >= ROU_SQ(ROU_R_RIM))       return ROU_C_GOLD;
  if (D >= ROU_SQ(ROU_R_TRACK_IN))  return ROU_C_TRACK;
  if (D >= ROU_SQ(ROU_R_APRON_OUT)) return ROU_C_GOLD;
  if (D >= ROU_SQ(ROU_R_APRON_IN)) {
    for (int k = 0; k < 8; k++) {
      if (abs(X - rouDiaX2[k]) + abs(Y - rouDiaY2[k]) <= ROU_DIAMOND_SZ) return ROU_C_GOLD;
    }
    return ROU_C_APRON;
  }
  if (D >= ROU_SQ(ROU_R_NUM_OUT))   return ROU_C_GOLD;
  if (D >= ROU_SQ(ROU_R_POCK_IN)) {
    float r  = 0.5f * sqrtf((float)D);
    float th = rouWrap(rouAtan2((float)X, (float)-Y));
    float u  = th / ROU_SLOT + 0.5f;
    int s    = (int)u;
    float f  = u - s;
    float edge = ((f < 0.5f) ? f : 1.0f - f) * ROU_SLOT * r;
    if (edge < ROU_FRET_PX) return ROU_C_FRET;
    s %= 37;
    if (s == rouHl) return rouPocketHi(ROU_WHEEL[s]);
    return rouPocketCol(ROU_WHEEL[s], D < ROU_SQ(ROU_R_NUM_IN));
  }
  if (D >= ROU_SQ(ROU_R_CONE))      return ROU_C_GOLD;
  if (D >= ROU_SQ(ROU_R_HUB))       return ROU_C_CONE;
  return ROU_C_HUB;
}

// ---------------------------------------------------------------- drawing
static void rouLabel(int s) {
  char b[4];
  snprintf(b, sizeof(b), "%d", ROU_WHEEL[s]);
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE);                  // transparent: background stays
  tft.setCursor(rouLx[s], rouLy[s]);
  tft.print(b);
}

static void rouLabels(int x0, int y0, int w, int h) {
  for (int s = 0; s < 37; s++) {
    if (rouLx[s] + rouLw[s] < x0 || rouLx[s] > x0 + w) continue;
    if (rouLy[s] + 7 < y0 || rouLy[s] > y0 + h) continue;
    rouLabel(s);
  }
}

// Repaint a rectangle of the wheel exactly as it was drawn
static void rouRestore(int x0, int y0, int w, int h) {
  if (x0 < 0) { w += x0; x0 = 0; }
  if (y0 < 0) { h += y0; y0 = 0; }
  if (x0 + w > 240) w = 240 - x0;
  if (y0 + h > 240) h = 240 - y0;
  if (w <= 0 || h <= 0) return;

  static uint16_t row[240];
  for (int y = y0; y < y0 + h; y++) {
    for (int i = 0; i < w; i++) row[i] = rouBg(x0 + i, y);
    tft.drawRGBBitmap(x0, y, row, w, 1);
  }
  rouLabels(x0, y0, w, h);
}

static void rouDrawWheel() {
  rouRestore(0, 0, 240, 240);
}

// Repaint one pocket (number band + pocket), e.g. to flash it
static void rouRestoreSlot(int s) {
  float a0 = (s - 0.5f) * ROU_SLOT, a1 = (s + 0.5f) * ROU_SLOT;
  float xs[5], ys[5];
  float rr[2] = {ROU_R_POCK_IN, ROU_R_NUM_OUT};
  int k = 0;
  for (int i = 0; i < 2; i++) {
    xs[k] = ROU_R_SCREEN + rr[i] * sinf(a0); ys[k++] = ROU_R_SCREEN - rr[i] * cosf(a0);
    xs[k] = ROU_R_SCREEN + rr[i] * sinf(a1); ys[k++] = ROU_R_SCREEN - rr[i] * cosf(a1);
  }
  float am = s * ROU_SLOT;                  // outer edge bulges at the middle
  xs[k] = ROU_R_SCREEN + ROU_R_NUM_OUT * sinf(am); ys[k++] = ROU_R_SCREEN - ROU_R_NUM_OUT * cosf(am);
  float x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
  for (int i = 1; i < k; i++) {
    if (xs[i] < x0) x0 = xs[i];
    if (xs[i] > x1) x1 = xs[i];
    if (ys[i] < y0) y0 = ys[i];
    if (ys[i] > y1) y1 = ys[i];
  }
  rouRestore((int)x0 - 1, (int)y0 - 1, (int)(x1 - x0) + 3, (int)(y1 - y0) + 3);
}

static void rouDrawBall(int x, int y) {
  tft.fillCircle(x, y, ROU_BALL_R, ROU_C_BALL_EDGE);
  tft.fillCircle(x - 1, y - 1, ROU_BALL_R - 1, C_WHITE);
}

static void rouEraseBall(int x, int y) {
  rouRestore(x - ROU_BALL_R - 1, y - ROU_BALL_R - 1, 2 * ROU_BALL_R + 3, 2 * ROU_BALL_R + 3);
}

// Four-armed gold turret, drawn off-screen then pushed in one go
static void rouDrawTurret(GFXcanvas16* cv, float phi) {
  if (!cv || !cv->getBuffer()) return;
  const int m = ROU_TURRET / 2;
  cv->fillScreen(ROU_C_HUB);
  for (int k = 0; k < 4; k++) {
    float a = phi + k * PI / 2.0f;
    float dx = sinf(a), dy = -cosf(a);
    int tx = m + (int)lroundf(21.0f * dx);
    int ty = m + (int)lroundf(21.0f * dy);
    int px = (int)lroundf(3.0f * -dy);
    int py = (int)lroundf(3.0f * dx);
    cv->fillTriangle(m + px, m + py, m - px, m - py, tx, ty, ROU_C_GOLD);
    cv->fillCircle(tx, ty, 3, ROU_C_GOLD);
    cv->drawPixel(tx - 1, ty - 1, ROU_C_SHINE);
  }
  cv->fillCircle(m, m, 8, ROU_C_GOLD);
  cv->fillCircle(m - 2, m - 2, 3, ROU_C_SHINE);
  tft.drawRGBBitmap(120 - m, 120 - m, cv->getBuffer(), ROU_TURRET, ROU_TURRET);
}

static void rouTiny(const char* s, int cy, uint16_t col) {
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(col);
  int w = (int)strlen(s) * 6 - 1;
  tft.setCursor(120 - w / 2, cy - 4);
  tft.print(s);
}

static void rouPanelBase(uint16_t fill) {
  tft.fillCircle(120, 120, ROU_PANEL_R, fill);
  tft.drawCircle(120, 120, ROU_PANEL_R, ROU_C_GOLD);
  tft.drawCircle(120, 120, ROU_PANEL_R - 1, ROU_C_GOLD);
}

static void rouDrawBetPanel(int bet, int streak) {
  rouPanelBase(ROU_C_PANEL);
  rouTiny(bet == 0 ? "MODE" : "YOUR BET", 98, C_DIMTEXT);
  uint16_t col = (bet == 0) ? ROU_C_GOLD : (bet == 1) ? ROU_C_BET_RED
               : (bet == 2) ? C_WHITE : C_YELLOW;
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextSize(1);
  const GFXfont* f = (textWidth(ROU_BET_NAME[bet]) <= 60) ? &FreeSansBold12pt7b
                                                          : &FreeSansBold9pt7b;
  drawCentered(ROU_BET_NAME[bet], 119, f, 1, col);
  if (bet == 0) {
    rouTiny("just spin", 140, C_RINGDIM);
  } else {
    char b[16];
    snprintf(b, sizeof(b), "wins %d", streak);
    rouTiny(b, 140, streak > 0 ? C_YELLOW : C_RINGDIM);
  }
}

// Live readout while the ball runs through the pockets
static void rouDrawLive(int n) {
  rouPanelBase(rouPocketCol(n, false));
  char b[4];
  snprintf(b, sizeof(b), "%d", n);
  drawCentered(b, 120, &FreeSansBold18pt7b, 1, C_WHITE);
}

// NORMAL mode (bet 0) shows the colour instead of win / lose
static void rouDrawResult(int n, int bet, bool win) {
  rouPanelBase(rouPocketCol(n, false));
  char b[4];
  snprintf(b, sizeof(b), "%d", n);
  drawCentered(b, 112, &FreeSansBold18pt7b, 1, C_WHITE);
  if (bet == 0) {
    const char* c = (n == 0) ? "GREEN" : rouIsRed(n) ? "RED" : "BLACK";
    drawCentered(c, 142, &FreeSans9pt7b, 1, C_WHITE);
  } else {
    drawCentered(win ? "WIN" : "LOSE", 142, &FreeSansBold9pt7b, 1, win ? C_YELLOW : C_WHITE);
  }
}

// Progress dots round the panel while the spin tilt is held
static void rouHoldRing(int dots, int &last) {
  if (dots == last) return;
  int from = last;
  if (dots < last) {
    for (int i = 0; i < ROU_RING_DOTS; i++) {
      float a = i * 2.0f * PI / ROU_RING_DOTS;
      tft.fillCircle(120 + (int)lroundf(ROU_RING_R * sinf(a)),
                     120 - (int)lroundf(ROU_RING_R * cosf(a)), 2, ROU_C_HUB);
    }
    from = 0;
  }
  for (int i = from; i < dots; i++) {
    float a = i * 2.0f * PI / ROU_RING_DOTS;
    tft.fillCircle(120 + (int)lroundf(ROU_RING_R * sinf(a)),
                   120 - (int)lroundf(ROU_RING_R * cosf(a)), 2, C_YELLOW);
  }
  last = dots;
}

static void rouHelp() {
  tft.fillScreen(C_BLACK);
  drawMultiline("Roulette", 72, 170, C_YELLOW);
  drawCentered("flick = mode / bet", 120, &FreeSans9pt7b, 1, C_WHITE);
  drawCentered("hold sideways = spin", 146, &FreeSans9pt7b, 1, C_WHITE);
  drawCentered("win = spin again", 180, &FreeSans9pt7b, 1, C_DIMTEXT);
  uint32_t t0 = millis();
  while (millis() - t0 < ROU_HELP_MS) {
    if (shakePoll()) return;
    delay(20);
  }
}

// ---------------------------------------------------------------- the spin
// Returns the pocket slot the ball settles in, or -1 on a shake.
// (bx, by) tracks where the ball is drawn so the caller can erase it later.
//
// Fairness: the physics runs in its own frame (th), and the whole run is
// shown turned by a random whole number of pockets (rot). Frets repeat
// every pocket, so the bounces look identical, and the random turn makes
// all 37 results exactly equally likely, whatever the physics prefers.
static int rouSpin(GFXcanvas16* cv, int &bx, int &by) {
  float rot = (esp_random() % 37) * ROU_SLOT;
  float th = (esp_random() % 3600) / 3600.0f * 2.0f * PI;
  float w  = ROU_W_MIN + (esp_random() % 1000) / 1000.0f * (ROU_W_MAX - ROU_W_MIN);
  float r  = ROU_BALL_TRACK, vr = 0.0f;
  float phi = 0.0f, ww = -ROU_WHEEL_W;        // wheel turns against the ball
  float hop = ROU_HOP0, hopPh = 0.0f;
  float target = 0.0f;
  int phase = 0;                              // 0 track, 1 apron, 2 pockets, 3 settle
  int lastSlot = -1, liveSlot = -1, settleN = 0;
  bool diaHit = false;
  uint32_t pockStart = 0;

  uint32_t t0 = millis();
  while (millis() - t0 < ROU_SPIN_MAX_MS + ROU_POCK_MAX_MS) {
    uint32_t frameStart = millis();
    if (shakePoll()) return -1;

    switch (phase) {
      case 0:                                  // racing round the track
        th += w;
        w *= ROU_W_DECAY;
        if (w < ROU_W_DROP) phase = 1;
        break;

      case 1:                                  // falling down the apron
        th += w;
        w *= 0.985f;
        vr -= ROU_FALL_G;
        r += vr;
        if (!diaHit && r < ROU_DIAMOND_R + 2.0f && r > ROU_DIAMOND_R - 4.0f) {
          float a = fmodf(rouWrap(th), PI / 4.0f);
          float d = ((a < PI / 8.0f) ? a : PI / 4.0f - a) * r;
          if (d < 5.0f) {                      // clipped a diamond: knocked about
            diaHit = true;
            vr = fabsf(vr) * 0.35f;
            w *= 0.65f + (esp_random() % 500) / 1000.0f;
          }
        }
        if (r > ROU_BALL_TRACK) r = ROU_BALL_TRACK;
        if (r <= ROU_BALL_POCK) {
          r = ROU_BALL_POCK;
          phase = 2;
          lastSlot = rouSlotAt(th);
          pockStart = millis();
        }
        break;

      case 2: {                                // the suspense part
        // every fret is a small hill: the ball is pushed towards the
        // nearest pocket centre, so a slow ball stalls on top of a fret
        // before tipping one way or the other, then rocks to rest.
        w -= ROU_FRET_HILL * sinf(2.0f * PI * th / ROU_SLOT);
        w *= ROU_POCK_DAMP;
        th += w;

        int s = rouSlotAt(th);
        if (s != lastSlot) {                   // just went over a fret
          lastSlot = s;
          float v = fabsf(w) / ROU_SLOT;       // pockets per frame
          float kick = v * 9.0f;
          if (kick > 4.5f) kick = 4.5f;
          if (kick > hop * sinf(hopPh)) { hop = kick; hopPh = 0.0f; }
          if (v > 0.25f && esp_random() % 100 < 12) w = -w * 0.45f;   // bounced back
        }
        hopPh += 0.32f;
        if (hopPh > PI) { hopPh -= PI; hop *= ROU_HOP_DAMP; }
        r = ROU_BALL_POCK + hop * sinf(hopPh);

        float off = th / ROU_SLOT - roundf(th / ROU_SLOT);
        bool atRest = fabsf(w) < ROU_W_STOP && fabsf(off) < ROU_CENTRE_TOL && hop < 0.5f;
        if (atRest || millis() - pockStart > ROU_POCK_MAX_MS) {
          phase = 3;
          target = rouWrap(roundf(th / ROU_SLOT) * ROU_SLOT);
        }
        break;
      }

      default: {                               // easing into the pocket centre
        float d = target - th;                 // shortest way round past 0
        if (d >  PI) d -= 2.0f * PI;
        if (d < -PI) d += 2.0f * PI;
        th += d * 0.3f;
        r  += (ROU_BALL_POCK - r) * 0.3f;
        if (++settleN >= 12) { th = target; r = ROU_BALL_POCK; }
        break;
      }
    }
    th = rouWrap(th);
    float shown = th + rot;

    // centre: spinning turret until the ball reaches the pockets,
    // then the number under the ball, live
    if (phase < 2) {
      phi += ww;
      ww *= 0.997f;
      rouDrawTurret(cv, phi);
    } else {
      int ls = rouSlotAt(shown);
      if (ls != liveSlot) { liveSlot = ls; rouDrawLive(ROU_WHEEL[ls]); }
    }

    // ball: erase old spot from the wheel function, draw the new one
    int ix = (int)lroundf(ROU_R_SCREEN + r * sinf(shown));
    int iy = (int)lroundf(ROU_R_SCREEN - r * cosf(shown));
    if (ix != bx || iy != by) {
      if (bx >= 0) rouEraseBall(bx, by);
      rouDrawBall(ix, iy);
      bx = ix;
      by = iy;
    }

    if (phase == 3 && settleN >= 12) return rouSlotAt(shown);

    uint32_t el = millis() - frameStart;
    if (el < FRAME_MS) delay(FRAME_MS - el);
  }
  return rouSlotAt(th + rot);                  // safety net, should never hit
}

// Flash the pocket the ball stopped in, then leave it lit
static bool rouRevealSlot(int slot, int bx, int by) {
  delay(350);
  for (int i = 0; i < ROU_BLINKS * 2 + 1; i++) {
    rouHl = (i % 2 == 0) ? slot : -1;
    rouRestoreSlot(slot);
    rouDrawBall(bx, by);
    uint32_t t = millis();
    while (millis() - t < 170) {
      if (shakePoll()) return false;
      delay(10);
    }
  }
  return true;                                 // ends lit (rouHl == slot)
}

static void rouClearHighlight() {
  if (rouHl < 0) return;
  int s = rouHl;
  rouHl = -1;
  rouRestoreSlot(s);
}

// =====================================================================
int playRoulette() {
  if (DEBUG_USB) Serial.println("Roulette start");
  rouInit();
  rouHl = -1;

  static bool helpShown = false;               // once per wake
  static int bet = 0;                          // remembered between rounds
  if (!helpShown) {
    helpShown = true;
    shakeReset();
    rouHelp();
    if (shakeSeen()) return 0;
  }

  rouDrawWheel();
  GFXcanvas16* cv = new GFXcanvas16(ROU_TURRET, ROU_TURRET);
  if (cv && !cv->getBuffer()) { delete cv; cv = NULL; }   // no RAM: skip the turret
  rouDrawTurret(cv, 0.0f);

  int streak = 0;
  int bx = -1, by = -1;                        // ball not on screen yet
  shakeReset();

  while (true) {
    // ---------------- choose mode / bet ----------------
    rouDrawBetPanel(bet, streak);
    int lastDots = 0;
    bool holding = false, needLevel = true, spin = false, quit = false;
    uint32_t holdStart = 0, lastAct = millis();

    while (!spin) {
      float up, right;
      tiltBoth(up, right);                     // also feeds shake-to-quit
      if (shakeSeen()) { quit = true; break; }

      if (menuFlick()) {                       // flick = next mode / bet (wraps)
        bet = (bet + 1) % ROU_NUM_BETS;
        rouHoldRing(0, lastDots);
        rouDrawBetPanel(bet, streak);
        holding = false;
        lastAct = millis();
      }

      // you arrive here still tilted from the menu: level out first
      if (fabsf(right) < MENU_PICK_DEG * 0.5f) needLevel = false;
      bool sideways = !needLevel && fabsf(right) > MENU_PICK_DEG;

      if (sideways) {
        if (!holding) { holding = true; holdStart = millis(); }
        float frac = (millis() - holdStart) / (float)MENU_HOLD_MS;
        if (frac >= 1.0f) spin = true;
        else rouHoldRing((int)(frac * ROU_RING_DOTS), lastDots);
        lastAct = millis();
      } else if (holding) {
        holding = false;
        rouHoldRing(0, lastDots);
      }

      if (millis() - lastAct > IDLE_ABORT_MS) {
        if (DEBUG_USB) Serial.println("Idle abort");
        quit = true;
        break;
      }
      delay(FRAME_MS);
    }
    if (quit) break;

    // ---------------- spin ----------------
    rouHoldRing(0, lastDots);
    rouRestore(120 - ROU_RING_R - 4, 120 - ROU_RING_R - 4,
               2 * ROU_RING_R + 9, 2 * ROU_RING_R + 9);     // panel -> bare hub
    rouClearHighlight();                                    // last round's pocket
    if (bx >= 0) { rouEraseBall(bx, by); bx = -1; by = -1; }

    int slot = rouSpin(cv, bx, by);
    if (slot < 0) break;                       // shaken mid-spin
    if (!rouRevealSlot(slot, bx, by)) break;

    int n = ROU_WHEEL[slot];
    bool win = rouWins(bet, n);
    rouDrawResult(n, bet, win);
    if (DEBUG_USB) Serial.printf("Roulette: %s, ball on %d (%s)%s\n",
                                 ROU_BET_NAME[bet], n,
                                 n == 0 ? "green" : rouIsRed(n) ? "red" : "black",
                                 bet == 0 ? "" : win ? " -> WIN" : " -> lose");

    uint32_t t1 = millis();
    while (millis() - t1 < ROU_RESULT_MS) {
      if (shakePoll()) break;
      delay(20);
    }
    if (shakeSeen()) break;
    if (bet == 0) continue;                    // NORMAL: just spin again
    if (!win) break;
    streak++;
  }

  if (cv) delete cv;
  return streak;
}
