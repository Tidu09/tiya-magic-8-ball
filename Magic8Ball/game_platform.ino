// =====================================================================
//  game_platform.ino — auto-scrolling platformer
//
//  Tilt RIGHT / LEFT : run (right = forward)
//  JERK UP           : jump (soft — tune with PF_JUMP_G)
//  Shake             : quit to the menu
//
//  Scoring follows the arcade convention: distance pays nothing, it only
//  drives the difficulty. Points come from optional risk —
//      coin  = 1
//      stomp = 2
//  The world slides left on its own, so standing still drifts you off the
//  back of the screen. Keep running.
// =====================================================================

// ---- play band (kept inside the round glass) ----
#define PF_TOP        40
#define PF_FLOOR      168           // default ground height
#define PF_CEIL       84            // highest a platform may sit
#define PF_DEAD_Y     210           // fallen below this = dead
#define PF_LEFT_DEAD  16            // dragged past this = fell behind
#define PF_RIGHT_MAX  184           // cannot outrun the camera
#define PF_HERO_W     11
#define PF_HERO_H     15

// ---- feel ----
#define PF_GRAV       0.62f
#define PF_JUMP_V     -8.4f
#define PF_RUN_ACC    0.030f        // per degree of tilt, per frame
#define PF_FRICTION   0.86f
#define PF_VX_MAX     3.4f
#define PF_COYOTE_MS  110           // grace to still jump just after a ledge
#define PF_STOMP_V    -5.2f         // bounce after squashing an enemy

// ---- jump gesture (independent of the menu's flick, so tune freely) ----
#define PF_JUMP_G     0.28f         // jerk this hard (g off rest) to jump. LOWER = more sensitive
#define PF_JUMP_REARM 0.14f         // must settle back under this before the next jump
#define PF_JUMP_GAP   110           // ms lockout after a jump, swallows the rebound
#define PF_JUMP_DIR   1             // 1 = up jerk only, -1 = flip, 0 = any direction

// ---- difficulty ----
#define PF_SCROLL0    1.40f
#define PF_SCROLL_UP  0.000016f     // gain per ms survived
#define PF_SCROLL_MAX 3.20f

// ---- world pieces ----
#define PF_MAX_PLAT   7
#define PF_MAX_COIN   6
#define PF_MAX_FOE    3
#define PF_PLAT_H     8
#define PF_PLAT_MINW  48
#define PF_PLAT_MAXW  94
#define PF_GAP_MIN    24
#define PF_GAP_MAX    50
#define PF_STEP_MAX   32            // largest height change between platforms
#define PF_COIN_R     5
#define PF_FOE_W      11
#define PF_FOE_H      11
#define PF_FOE_SPD    0.42f

#define C_GROUND      C_GAME        // cyan, same as the other games
#define C_HERO        C_PLAYER      // yellow
#define C_FOE         0xFB2C        // salmon, clearly not the hero

struct PfPlat { float x; int y, w; bool live; int dx0, dy0, dw0; bool drawn; };
struct PfCoin { float x; int y; bool live, got; int dx0, dy0; bool drawn; };
struct PfFoe  { float x; int y; float vx; int plat; bool live; int dx0, dy0; bool drawn; };

static PfPlat pfPlat[PF_MAX_PLAT];
static PfCoin pfCoin[PF_MAX_COIN];
static PfFoe  pfFoe[PF_MAX_FOE];
static float  pfNextX;              // screen x where the next platform starts
static int    pfLastY;              // height of the previous platform

static inline int pfRand(int lo, int hi) {
  if (hi <= lo) return lo;
  return lo + (int)(esp_random() % (uint32_t)(hi - lo + 1));
}

// Clamped fill — keeps partly off-screen pieces from wrapping
static void pfFill(int x, int y, int w, int h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > 240) w = 240 - x;
  if (y + h > 240) h = 240 - y;
  if (w <= 0 || h <= 0) return;
  tft.fillRect(x, y, w, h, c);
}

// Clear a rect, then repaint any platform pixels that were under it, so a
// sprite moving across a platform never eats a hole in it.
static void pfRepair(int x0, int y0, int w, int h) {
  pfFill(x0, y0, w, h, C_BLACK);
  int x1 = x0 + w, y1 = y0 + h;
  for (int i = 0; i < PF_MAX_PLAT; i++) {
    if (!pfPlat[i].live || !pfPlat[i].drawn) continue;
    int ox0 = max(x0, pfPlat[i].dx0), ox1 = min(x1, pfPlat[i].dx0 + pfPlat[i].dw0);
    if (ox1 <= ox0) continue;
    int oy0 = max(y0, pfPlat[i].dy0), oy1 = min(y1, pfPlat[i].dy0 + PF_PLAT_H);
    if (oy1 <= oy0) continue;
    pfFill(ox0, oy0, ox1 - ox0, oy1 - oy0, C_GROUND);
  }
}

// ---------------------------------------------------------------- spawning
static void pfSpawn() {
  int slot = -1;
  for (int i = 0; i < PF_MAX_PLAT; i++) if (!pfPlat[i].live) { slot = i; break; }
  if (slot < 0) { pfNextX += 40; return; }

  int w = pfRand(PF_PLAT_MINW, PF_PLAT_MAXW);
  int y = pfLastY + pfRand(-PF_STEP_MAX, PF_STEP_MAX);
  if (y > PF_FLOOR) y = PF_FLOOR;
  if (y < PF_CEIL)  y = PF_CEIL;
  pfLastY = y;

  pfPlat[slot].x = pfNextX;
  pfPlat[slot].y = y;
  pfPlat[slot].w = w;
  pfPlat[slot].live = true;
  pfPlat[slot].drawn = false;

  int gap = pfRand(PF_GAP_MIN, PF_GAP_MAX);

  // a coin: usually hovering over the gap that follows (the risky one)
  for (int c = 0; c < PF_MAX_COIN; c++) {
    if (pfCoin[c].live) continue;
    bool overGap = (esp_random() % 100) < 60;
    pfCoin[c].x = overGap ? (pfNextX + w + gap / 2) : (pfNextX + w / 2);
    pfCoin[c].y = y - (overGap ? 30 : 26);
    if (pfCoin[c].y < PF_TOP + PF_COIN_R) pfCoin[c].y = PF_TOP + PF_COIN_R;
    pfCoin[c].live = true;
    pfCoin[c].got = false;
    pfCoin[c].drawn = false;
    break;
  }

  // sometimes an enemy patrolling this platform (needs room to walk)
  if (w >= 64 && (esp_random() % 100) < 45) {
    for (int f = 0; f < PF_MAX_FOE; f++) {
      if (pfFoe[f].live) continue;
      pfFoe[f].x = pfNextX + 6;
      pfFoe[f].y = y - PF_FOE_H;
      pfFoe[f].vx = PF_FOE_SPD;
      pfFoe[f].plat = slot;
      pfFoe[f].live = true;
      pfFoe[f].drawn = false;
      break;
    }
  }

  pfNextX += w + gap;
}

// ------------------------------------------------------------ jump gesture
// Own detector so PF_JUMP_G can be far softer than the menu's MENU_FLICK_G.
// Edge-triggered: must settle back near rest before the next jump counts.
static bool pfJump() {
  static bool armed = true;
  static uint32_t lastJump = 0;
  float a[3];
  if (!readAccelG(a)) return false;

  float du = (a[SCREEN_UP_AXIS] - refVec[SCREEN_UP_AXIS]) * SCREEN_UP_SIGN;
  float lift = (PF_JUMP_DIR == 0) ? fabsf(du) : du * PF_JUMP_DIR;

  if (armed && lift > PF_JUMP_G && millis() - lastJump > PF_JUMP_GAP) {
    armed = false;
    lastJump = millis();
    return true;
  }
  if (fabsf(du) < PF_JUMP_REARM) armed = true;
  return false;
}

// ---------------------------------------------------------------- drawing
static void pfDrawHero(int x, int y, bool dead) {
  uint16_t c = dead ? C_DANGER : C_HERO;
  pfFill(x + 2, y - 4, PF_HERO_W - 4, 4, c);          // head
  pfFill(x, y, PF_HERO_W, PF_HERO_H, c);              // body
  pfFill(x + PF_HERO_W - 4, y + 3, 2, 2, C_BLACK);    // eye
}

static void pfDrawFoe(int x, int y) {
  pfFill(x, y, PF_FOE_W, PF_FOE_H, C_FOE);
  pfFill(x + 2, y + 3, 2, 2, C_BLACK);
  pfFill(x + PF_FOE_W - 4, y + 3, 2, 2, C_BLACK);
}

// =====================================================================
int playPlatform() {
  int best = prefs.getUInt("bestPlat", 0);
  if (DEBUG_USB) Serial.println("Platformer start");

  tft.fillScreen(C_BLACK);
  tft.drawCircle(120, 120, 119, C_RINGDIM);
  drawHud(0, best);

  for (int i = 0; i < PF_MAX_PLAT; i++) pfPlat[i].live = false;
  for (int i = 0; i < PF_MAX_COIN; i++) pfCoin[i].live = false;
  for (int i = 0; i < PF_MAX_FOE;  i++) pfFoe[i].live  = false;

  // opening run-up: one long safe platform, then generated terrain
  pfLastY = PF_FLOOR;
  pfNextX = 10;
  pfPlat[0].x = 10;
  pfPlat[0].y = PF_FLOOR;
  pfPlat[0].w = 150;
  pfPlat[0].live = true;
  pfPlat[0].drawn = false;
  pfNextX = 10 + 150 + PF_GAP_MIN;
  while (pfNextX < 300) pfSpawn();

  float heroX = 64.0f;
  float heroY = PF_FLOOR - PF_HERO_H;
  float vx = 0, vy = 0;
  int hx0 = (int)heroX, hy0 = (int)heroY;
  bool heroDrawn = false;
  bool onGround = true;
  uint32_t lastGround = millis();

  int score = 0, shownScore = -1;
  bool alive = true;

  uint32_t startMs = millis();
  uint32_t lastMoveMs = millis();
  shakeReset();

  while (alive && millis() - startMs < MAX_PLAY_MS) {
    uint32_t frameStart = millis();

    // ---------------- input ----------------
    float up, right;
    tiltBoth(up, right);            // also feeds the shake-to-quit detector
    bool flick = pfJump();          // soft up-jerk = jump

    if (shakeSeen()) break;
    if (fabsf(right) > IDLE_TILT_DEG || flick) lastMoveMs = millis();
    if (millis() - lastMoveMs > IDLE_ABORT_MS) {
      if (DEBUG_USB) Serial.println("Idle abort");
      break;
    }

    float scroll = PF_SCROLL0 + (millis() - startMs) * PF_SCROLL_UP;
    if (scroll > PF_SCROLL_MAX) scroll = PF_SCROLL_MAX;

    // ---------------- erase everything that moves ----------------
    // (done before any new drawing, using the OLD platform positions)
    if (heroDrawn) pfRepair(hx0, hy0 - 4, PF_HERO_W, PF_HERO_H + 4);
    for (int i = 0; i < PF_MAX_FOE; i++)
      if (pfFoe[i].drawn) pfRepair(pfFoe[i].dx0, pfFoe[i].dy0, PF_FOE_W, PF_FOE_H);
    for (int i = 0; i < PF_MAX_COIN; i++)
      if (pfCoin[i].drawn)
        pfRepair(pfCoin[i].dx0 - PF_COIN_R, pfCoin[i].dy0 - PF_COIN_R,
                 2 * PF_COIN_R + 1, 2 * PF_COIN_R + 1);

    // ---------------- hero physics ----------------
    vx += right * PF_RUN_ACC;
    vx *= PF_FRICTION;
    if (vx >  PF_VX_MAX) vx =  PF_VX_MAX;
    if (vx < -PF_VX_MAX) vx = -PF_VX_MAX;

    if (flick && (onGround || millis() - lastGround < PF_COYOTE_MS)) {
      vy = PF_JUMP_V;
      onGround = false;
    }
    vy += PF_GRAV;
    if (vy > 9.0f) vy = 9.0f;

    float prevFoot = heroY + PF_HERO_H;
    float nextX = heroX + vx - scroll;      // scroll drags the hero back
    float nextY = heroY + vy;

    // ---------------- scroll the world ----------------
    for (int i = 0; i < PF_MAX_PLAT; i++) if (pfPlat[i].live) pfPlat[i].x -= scroll;
    for (int i = 0; i < PF_MAX_COIN; i++) if (pfCoin[i].live) pfCoin[i].x -= scroll;
    for (int i = 0; i < PF_MAX_FOE;  i++) if (pfFoe[i].live)  pfFoe[i].x  -= scroll;
    pfNextX -= scroll;

    // ---------------- land on platforms (from above only) ----------------
    onGround = false;
    if (vy >= 0) {
      for (int i = 0; i < PF_MAX_PLAT; i++) {
        if (!pfPlat[i].live) continue;
        float px = pfPlat[i].x;
        if (nextX + PF_HERO_W < px || nextX > px + pfPlat[i].w) continue;
        float top = (float)pfPlat[i].y;
        if (prevFoot <= top + 2.0f && nextY + PF_HERO_H >= top) {
          nextY = top - PF_HERO_H;
          vy = 0;
          onGround = true;
          lastGround = millis();
          break;
        }
      }
    }

    heroX = nextX;
    heroY = nextY;
    if (heroX > PF_RIGHT_MAX) { heroX = PF_RIGHT_MAX; vx = 0; }
    if (heroY < PF_TOP) { heroY = PF_TOP; vy = 0; }
    if (heroY > PF_DEAD_Y || heroX < PF_LEFT_DEAD) alive = false;

    // ---------------- coins ----------------
    for (int i = 0; i < PF_MAX_COIN; i++) {
      if (!pfCoin[i].live) continue;
      if (!pfCoin[i].got) {
        float dx = pfCoin[i].x - (heroX + PF_HERO_W / 2.0f);
        float dy = pfCoin[i].y - (heroY + PF_HERO_H / 2.0f);
        if (fabsf(dx) < 11.0f && fabsf(dy) < 13.0f) {
          pfCoin[i].got = true;
          score++;
        }
      }
      if (pfCoin[i].x < -PF_COIN_R - 2) pfCoin[i].live = false;
    }

    // ---------------- enemies ----------------
    for (int i = 0; i < PF_MAX_FOE; i++) {
      if (!pfFoe[i].live) continue;
      PfPlat& p = pfPlat[pfFoe[i].plat];
      if (!p.live) { pfFoe[i].live = false; continue; }

      pfFoe[i].x += pfFoe[i].vx;
      if (pfFoe[i].x < p.x + 2) { pfFoe[i].x = p.x + 2; pfFoe[i].vx = PF_FOE_SPD; }
      if (pfFoe[i].x + PF_FOE_W > p.x + p.w - 2) {
        pfFoe[i].x = p.x + p.w - 2 - PF_FOE_W;
        pfFoe[i].vx = -PF_FOE_SPD;
      }
      pfFoe[i].y = p.y - PF_FOE_H;

      bool hit = !(heroX + PF_HERO_W < pfFoe[i].x ||
                   heroX > pfFoe[i].x + PF_FOE_W ||
                   heroY + PF_HERO_H < pfFoe[i].y ||
                   heroY > pfFoe[i].y + PF_FOE_H);
      if (hit) {
        bool stomp = (vy > 0) && (prevFoot <= pfFoe[i].y + 5);
        if (stomp) {
          if (pfFoe[i].drawn) pfRepair(pfFoe[i].dx0, pfFoe[i].dy0, PF_FOE_W, PF_FOE_H);
          pfFoe[i].live = false;
          pfFoe[i].drawn = false;
          score += 2;                       // stomps beat coins
          vy = PF_STOMP_V;
        } else {
          alive = false;
        }
      }
      if (pfFoe[i].live && pfFoe[i].x < -PF_FOE_W - 2) pfFoe[i].live = false;
    }

    // ---------------- retire + spawn platforms ----------------
    for (int i = 0; i < PF_MAX_PLAT; i++) {
      if (pfPlat[i].live && pfPlat[i].x + pfPlat[i].w < -2) {
        if (pfPlat[i].drawn) pfFill(pfPlat[i].dx0, pfPlat[i].dy0, pfPlat[i].dw0, PF_PLAT_H, C_BLACK);
        pfPlat[i].live = false;
        pfPlat[i].drawn = false;
      }
    }
    while (pfNextX < 260) pfSpawn();

    // ---------------- draw platforms (sliver: they only translate) -------
    for (int i = 0; i < PF_MAX_PLAT; i++) {
      if (!pfPlat[i].live) continue;
      int nx = (int)lroundf(pfPlat[i].x);
      if (!pfPlat[i].drawn) {
        pfFill(nx, pfPlat[i].y, pfPlat[i].w, PF_PLAT_H, C_GROUND);
      } else {
        int dx = pfPlat[i].dx0 - nx;
        if (dx > 0 && dx < pfPlat[i].dw0) {
          pfFill(nx, pfPlat[i].y, dx, PF_PLAT_H, C_GROUND);                    // new left strip
          pfFill(nx + pfPlat[i].w, pfPlat[i].dy0, dx, PF_PLAT_H, C_BLACK);     // vacated right strip
        } else if (dx != 0) {
          pfFill(pfPlat[i].dx0, pfPlat[i].dy0, pfPlat[i].dw0, PF_PLAT_H, C_BLACK);
          pfFill(nx, pfPlat[i].y, pfPlat[i].w, PF_PLAT_H, C_GROUND);
        }
      }
      pfPlat[i].dx0 = nx;
      pfPlat[i].dy0 = pfPlat[i].y;
      pfPlat[i].dw0 = pfPlat[i].w;
      pfPlat[i].drawn = true;
    }

    // ---------------- draw sprites on top ----------------
    for (int i = 0; i < PF_MAX_COIN; i++) {
      pfCoin[i].drawn = false;
      if (!pfCoin[i].live || pfCoin[i].got) continue;
      int nx = (int)lroundf(pfCoin[i].x);
      if (nx < -PF_COIN_R || nx > 240 + PF_COIN_R) continue;
      tft.fillCircle(nx, pfCoin[i].y, PF_COIN_R, C_FOOD);
      pfCoin[i].dx0 = nx;
      pfCoin[i].dy0 = pfCoin[i].y;
      pfCoin[i].drawn = true;
    }

    for (int i = 0; i < PF_MAX_FOE; i++) {
      pfFoe[i].drawn = false;
      if (!pfFoe[i].live) continue;
      int nx = (int)lroundf(pfFoe[i].x);
      pfDrawFoe(nx, pfFoe[i].y);
      pfFoe[i].dx0 = nx;
      pfFoe[i].dy0 = pfFoe[i].y;
      pfFoe[i].drawn = true;
    }

    hx0 = (int)lroundf(heroX);
    hy0 = (int)lroundf(heroY);
    pfDrawHero(hx0, hy0, !alive);
    heroDrawn = true;

    if (score != shownScore) { drawHud(score, best); shownScore = score; }

    uint32_t el = millis() - frameStart;
    if (el < FRAME_MS) delay(FRAME_MS - el);
  }

  pfDrawHero(hx0, hy0, true);
  delay(350);
  return score;
}
