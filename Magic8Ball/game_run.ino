// =====================================================================
//  game_run.ino — tilt runner: steer the dot through the gaps
//  Only the slivers that actually change are repainted each frame.
// =====================================================================

float  gWallX[MAX_WALLS];
int    gWallPrevX[MAX_WALLS];
int    gWallGapC[MAX_WALLS];
int    gWallGapH[MAX_WALLS];
bool   gWallLive[MAX_WALLS];
bool   gWallPassed[MAX_WALLS];
bool   gWallFresh[MAX_WALLS];

// Paint a vertical strip of a wall (skips the gap)
static void paintWallStrip(int x, int w, int gapC, int gapH, uint16_t color) {
  if (w <= 0) return;
  int top = gapC - gapH / 2;
  int bot = gapC + gapH / 2;
  if (top > PLAY_TOP) tft.fillRect(x, PLAY_TOP, w, top - PLAY_TOP, color);
  if (bot < PLAY_BOT) tft.fillRect(x, bot, w, PLAY_BOT - bot, color);
}

// Clear a small region, then repaint any wall pixels inside it
static void repairRegion(int x0, int y0, int w, int h) {
  tft.fillRect(x0, y0, w, h, C_BLACK);
  int x1 = x0 + w, y1 = y0 + h;
  for (int i = 0; i < MAX_WALLS; i++) {
    if (!gWallLive[i] || gWallFresh[i]) continue;
    int wx = gWallPrevX[i];
    int ox0 = max(x0, wx), ox1 = min(x1, wx + WALL_W);
    if (ox1 <= ox0) continue;
    int top = gWallGapC[i] - gWallGapH[i] / 2;
    int bot = gWallGapC[i] + gWallGapH[i] / 2;
    int ay0 = max(y0, PLAY_TOP), ay1 = min(y1, top);
    if (ay1 > ay0) tft.fillRect(ox0, ay0, ox1 - ox0, ay1 - ay0, C_GAME);
    int by0 = max(y0, bot), by1 = min(y1, PLAY_BOT);
    if (by1 > by0) tft.fillRect(ox0, by0, ox1 - ox0, by1 - by0, C_GAME);
  }
}

int playRunner() {
  int best = prefs.getUInt("bestRun", 0);

  if (DEBUG_USB) Serial.println("Runner start");
  tft.fillScreen(C_BLACK);
  tft.drawCircle(120, 120, 119, C_RINGDIM);
  drawHud(0, best);

  for (int i = 0; i < MAX_WALLS; i++) {
    gWallLive[i] = false;
    gWallFresh[i] = false;
    gWallPrevX[i] = -99;
  }

  float playerY = PLAY_MID;
  int prevPlayerY = PLAY_MID;
  int score = 0, shownScore = -1;
  float speed = SPEED_START;
  float spawnGate = 0;
  bool alive = true;

  uint32_t startMs = millis();
  uint32_t lastMoveMs = millis();
  float prevTilt = 0;

  while (alive && millis() - startMs < MAX_PLAY_MS) {
    uint32_t frameStart = millis();

    speed = SPEED_START + score * SPEED_STEP;
    if (speed > SPEED_MAX) speed = SPEED_MAX;
    int gapNow = (int)(GAP_START - score * GAP_STEP);
    if (gapNow < GAP_MIN) gapNow = GAP_MIN;

    if (spawnGate <= 0) {
      for (int i = 0; i < MAX_WALLS; i++) {
        if (!gWallLive[i]) {
          int lo = PLAY_TOP + gapNow / 2 + 10;
          int hi = PLAY_BOT - gapNow / 2 - 10;
          gWallGapC[i] = lo + (int)(esp_random() % (uint32_t)(hi - lo + 1));
          gWallGapH[i] = gapNow;              // frozen for this wall's life
          gWallX[i] = 240;
          gWallPrevX[i] = 240;
          gWallLive[i] = true;
          gWallPassed[i] = false;
          gWallFresh[i] = true;
          spawnGate = WALL_SPACING;
          break;
        }
      }
    }
    spawnGate -= speed;

    float tilt = tiltUpDeg();
    if (fabsf(tilt) > IDLE_TILT_DEG || fabsf(tilt - prevTilt) > IDLE_TILT_DEG) lastMoveMs = millis();
    prevTilt = tilt;
    if (shakeSeen()) break;                    // shake = quit to menu
    if (millis() - lastMoveMs > IDLE_ABORT_MS) {
      if (DEBUG_USB) Serial.println("Idle abort");
      break;
    }

    float t = tilt / TILT_FULL_DEG;
    if (t > 1.2f) t = 1.2f;
    if (t < -1.2f) t = -1.2f;
    float target = PLAY_MID - t * ((PLAY_BOT - PLAY_TOP) / 2 - PLAYER_R - 2);
    playerY += (target - playerY) * 0.35f;
    if (playerY < PLAY_TOP + PLAYER_R) playerY = PLAY_TOP + PLAYER_R;
    if (playerY > PLAY_BOT - PLAYER_R) playerY = PLAY_BOT - PLAYER_R;

    repairRegion(PLAYER_X - PLAYER_R, prevPlayerY - PLAYER_R,
                 2 * PLAYER_R + 1, 2 * PLAYER_R + 1);

    for (int i = 0; i < MAX_WALLS; i++) {
      if (!gWallLive[i]) continue;
      int oldX = gWallPrevX[i];
      gWallX[i] -= speed;
      int newX = (int)lroundf(gWallX[i]);

      if (newX + WALL_W < 0) { gWallLive[i] = false; gWallPrevX[i] = -99; continue; }

      if (gWallFresh[i]) {
        paintWallStrip(newX, WALL_W, gWallGapC[i], gWallGapH[i], C_GAME);
        gWallFresh[i] = false;
      } else {
        int dx = oldX - newX;
        if (dx > 0) {
          paintWallStrip(newX, dx, gWallGapC[i], gWallGapH[i], C_GAME);
          tft.fillRect(newX + WALL_W, PLAY_TOP, dx, PLAY_BOT - PLAY_TOP, C_BLACK);
        }
      }
      gWallPrevX[i] = newX;

      if (!gWallPassed[i] && newX + WALL_W < PLAYER_X - PLAYER_R) {
        gWallPassed[i] = true;
        score++;
      }
      if (newX < PLAYER_X + PLAYER_R && newX + WALL_W > PLAYER_X - PLAYER_R) {
        if (fabsf(playerY - gWallGapC[i]) > gWallGapH[i] / 2 - PLAYER_R) alive = false;
      }
    }

    prevPlayerY = (int)playerY;
    tft.fillCircle(PLAYER_X, prevPlayerY, PLAYER_R, alive ? C_PLAYER : C_DANGER);

    if (score != shownScore) { drawHud(score, best); shownScore = score; }

    uint32_t el = millis() - frameStart;
    if (el < FRAME_MS) delay(FRAME_MS - el);
  }

  return score;
}
