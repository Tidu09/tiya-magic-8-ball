// =====================================================================
//  game_maze.ino — roll the marble through the ring gaps to the centre
//  Tilt gives acceleration, not position: the ball has real momentum.
// =====================================================================

static float ringR[MAZE_RINGS];
static float ringGapC[MAZE_RINGS];    // gap centre angle, radians
static float ringGapW;                // gap half-width, radians

static float wrapPi(float a) {
  while (a > PI) a -= 2 * PI;
  while (a < -PI) a += 2 * PI;
  return a;
}

static bool inGap(int i, float ang) {
  return fabsf(wrapPi(ang - ringGapC[i])) < ringGapW;
}

// Rings are drawn as dotted arcs (GFX has no arc primitive)
#define RING_STEPS 220
static float ringCos[RING_STEPS], ringSin[RING_STEPS];
static bool ringTablesReady = false;

static void buildRingTables() {
  if (ringTablesReady) return;
  for (int s = 0; s < RING_STEPS; s++) {
    float ang = s * 2.0f * PI / RING_STEPS - PI;
    ringCos[s] = cosf(ang);
    ringSin[s] = sinf(ang);
  }
  ringTablesReady = true;
}

static void drawRing(int i, uint16_t color) {
  buildRingTables();
  for (int s = 0; s < RING_STEPS; s++) {
    float ang = s * 2.0f * PI / RING_STEPS - PI;
    if (inGap(i, ang)) continue;
    tft.fillCircle(120 + (int)(ringR[i] * ringCos[s]),
                   120 + (int)(ringR[i] * ringSin[s]), 2, color);
  }
}

// Repaint any ring dots / goal ring that fall inside a rectangle the ball
// just erased, so rolling past a wall never eats it.
static void repairMazeArea(int x0, int y0, int w, int h) {
  buildRingTables();
  int x1 = x0 + w, y1 = y0 + h;
  for (int i = 0; i < MAZE_RINGS; i++) {
    for (int s = 0; s < RING_STEPS; s++) {
      int dx = (int)(ringR[i] * ringCos[s]);
      int dy = (int)(ringR[i] * ringSin[s]);
      int x = 120 + dx, y = 120 + dy;
      if (x + 2 < x0 || x - 2 > x1 || y + 2 < y0 || y - 2 > y1) continue;
      float ang = atan2f((float)dy, (float)dx);
      if (inGap(i, ang)) continue;
      tft.fillCircle(x, y, 2, C_GAME);
    }
  }
  // goal ring
  int gx0 = 120 - MAZE_GOAL_R - 2, gx1 = 120 + MAZE_GOAL_R + 2;
  if (!(x1 < gx0 || x0 > gx1 || y1 < gx0 || y0 > gx1)) {
    tft.drawCircle(120, 120, MAZE_GOAL_R, C_FOOD);
    tft.drawCircle(120, 120, MAZE_GOAL_R - 1, C_FOOD);
  }
}

static void buildMaze(int level) {
  ringGapW = MAZE_GAP_START - level * MAZE_GAP_STEP;
  if (ringGapW < 0.30f) ringGapW = 0.30f;
  for (int i = 0; i < MAZE_RINGS; i++) {
    ringR[i] = 98.0f - i * 28.0f;                      // 98, 70, 42
    ringGapC[i] = (esp_random() % 3600) / 3600.0f * 2 * PI - PI;
  }
}

static void drawMaze(int level, int score, int best) {
  tft.fillScreen(C_BLACK);
  tft.drawCircle(120, 120, 119, C_RINGDIM);
  for (int i = 0; i < MAZE_RINGS; i++) drawRing(i, C_GAME);
  tft.drawCircle(120, 120, MAZE_GOAL_R, C_FOOD);
  tft.drawCircle(120, 120, MAZE_GOAL_R - 1, C_FOOD);
  drawHud(score, best);
}

static void drawTime(uint32_t leftMs) {
  tft.fillRect(96, 214, 48, 22, C_BLACK);
  char b[8];
  snprintf(b, sizeof(b), "%lus", (unsigned long)(leftMs / 1000));
  drawCentered(b, 224, &FreeSans9pt7b, 1, leftMs < 10000 ? C_DANGER : C_DIMTEXT);
}

int playMaze() {
  int best = prefs.getUInt("bestMaze", 0);

  if (DEBUG_USB) Serial.println("Maze start");
  int level = 0, score = 0;
  buildMaze(level);
  drawMaze(level, score, best);

  // start just inside the rim, opposite the outer gap
  float startAng = wrapPi(ringGapC[0] + PI);
  float x = 120 + (MAZE_OUTER_R - 8) * cosf(startAng);
  float y = 120 + (MAZE_OUTER_R - 8) * sinf(startAng);
  float vx = 0, vy = 0;
  int px = (int)x, py = (int)y;
  tft.fillCircle(px, py, MAZE_BALL_R, C_PLAYER);

  uint32_t startMs = millis();
  uint32_t lastMoveMs = millis();
  uint32_t lastSec = 0;
  float prevUp = 0;

  while (millis() - startMs < MAZE_TIME_MS) {
    uint32_t frameStart = millis();

    float up, right;
    tiltBoth(up, right);
    if (fabsf(up) > IDLE_TILT_DEG || fabsf(right) > IDLE_TILT_DEG ||
        fabsf(up - prevUp) > IDLE_TILT_DEG) lastMoveMs = millis();
    prevUp = up;
    if (shakeSeen()) break;                    // shake = quit to menu
    if (millis() - lastMoveMs > IDLE_ABORT_MS) {
      if (DEBUG_USB) Serial.println("Idle abort");
      break;
    }

    // tilt -> acceleration. Vertical inverted to match snake:
    // tipping the TOP away rolls the marble DOWN the screen.
    vx += right * MAZE_ACCEL;
    vy += up * MAZE_ACCEL;
    vx *= MAZE_FRICTION;
    vy *= MAZE_FRICTION;
    float sp = sqrtf(vx * vx + vy * vy);
    if (sp > MAZE_VMAX) { vx = vx / sp * MAZE_VMAX; vy = vy / sp * MAZE_VMAX; }

    float prevR = sqrtf((x - 120) * (x - 120) + (y - 120) * (y - 120));
    x += vx;
    y += vy;

    float dxc = x - 120, dyc = y - 120;
    float r = sqrtf(dxc * dxc + dyc * dyc);
    float ang = atan2f(dyc, dxc);

    // outer boundary
    if (r > MAZE_OUTER_R - MAZE_BALL_R) {
      r = MAZE_OUTER_R - MAZE_BALL_R;
      float vr = vx * cosf(ang) + vy * sinf(ang);
      vx -= (1.0f + MAZE_BOUNCE) * vr * cosf(ang);
      vy -= (1.0f + MAZE_BOUNCE) * vr * sinf(ang);
      x = 120 + r * cosf(ang);
      y = 120 + r * sinf(ang);
    }

    // ring walls: blocked unless the ball is inside that ring's gap
    for (int i = 0; i < MAZE_RINGS; i++) {
      float R = ringR[i];
      if (inGap(i, ang)) continue;                       // gap: pass straight through

      bool overlap = fabsf(r - R) < (MAZE_BALL_R + 2);   // touching the wall band
      bool crossed = ((prevR >= R) != (r >= R));         // jumped over it this frame
      if (!overlap && !crossed) continue;

      bool fromOutside = (prevR >= R);
      r = fromOutside ? (R + MAZE_BALL_R + 2) : (R - MAZE_BALL_R - 2);

      float ca = cosf(ang), sa = sinf(ang);
      float vr = vx * ca + vy * sa;                      // radial component
      vx -= (1.0f + MAZE_BOUNCE) * vr * ca;
      vy -= (1.0f + MAZE_BOUNCE) * vr * sa;
      x = 120 + r * ca;
      y = 120 + r * sa;
    }

    // redraw ball only
    int nx = (int)lroundf(x), ny = (int)lroundf(y);
    if (nx != px || ny != py) {
      tft.fillCircle(px, py, MAZE_BALL_R, C_BLACK);
      repairMazeArea(px - MAZE_BALL_R - 2, py - MAZE_BALL_R - 2,
                     2 * MAZE_BALL_R + 4, 2 * MAZE_BALL_R + 4);
      px = nx;
      py = ny;
      tft.fillCircle(px, py, MAZE_BALL_R, C_PLAYER);
    }

    // goal reached
    if (r < MAZE_GOAL_R) {
      score++;
      level++;
      tft.fillCircle(120, 120, MAZE_GOAL_R, C_YELLOW);
      delay(250);
      buildMaze(level);
      drawMaze(level, score, best);
      startAng = wrapPi(ringGapC[0] + PI);
      x = 120 + (MAZE_OUTER_R - 8) * cosf(startAng);
      y = 120 + (MAZE_OUTER_R - 8) * sinf(startAng);
      vx = vy = 0;
      px = (int)x;
      py = (int)y;
      tft.fillCircle(px, py, MAZE_BALL_R, C_PLAYER);
      lastSec = 0;
    }

    uint32_t leftMs = MAZE_TIME_MS - (millis() - startMs);
    if (leftMs / 1000 != lastSec) { drawTime(leftMs); lastSec = leftMs / 1000; }

    uint32_t el = millis() - frameStart;
    if (el < FRAME_MS) delay(FRAME_MS - el);
  }

  return score;
}
