// =====================================================================
//  game_snake.ino — snake on a round 13x13 grid
//  Tilt sets the direction (absolute). Cells outside the circle are wall.
// =====================================================================

static int8_t snX[SNAKE_MAX], snY[SNAKE_MAX];
static int snLen, snHead;

static inline int cellPx(int c) { return GRID_ORIGIN + c * CELL; }
static inline int cellMid(int c) { return GRID_ORIGIN + c * CELL + CELL / 2; }

static bool cellValid(int cx, int cy) {
  if (cx < 0 || cy < 0 || cx >= GRID_N || cy >= GRID_N) return false;
  int dx = cellMid(cx) - 120;
  int dy = cellMid(cy) - 120;
  return (dx * dx + dy * dy) <= GRID_RADIUS * GRID_RADIUS;
}

// Walk off one side, come back on the other (round board, so the re-entry
// cell is the first valid one on the far side of that row/column)
static void wrapCell(int &nx, int &ny, int dx, int dy) {
  if (cellValid(nx, ny)) return;
  int px = nx, py = ny;
  while (px - dx >= 0 && py - dy >= 0 && px - dx < GRID_N && py - dy < GRID_N) {
    px -= dx;
    py -= dy;
  }
  while (!cellValid(px, py)) {
    px += dx;
    py += dy;
    if (px < 0 || py < 0 || px >= GRID_N || py >= GRID_N) return;
  }
  nx = px;
  ny = py;
}

static void paintCell(int cx, int cy, uint16_t color) {
  tft.fillRect(cellPx(cx) + 1, cellPx(cy) + 1, CELL - 2, CELL - 2, color);
}

static bool onSnake(int cx, int cy, int ignoreTail) {
  for (int i = 0; i < snLen; i++) {
    if (ignoreTail && i == snLen - 1) continue;      // tail moves away this step
    int idx = (snHead - i + SNAKE_MAX) % SNAKE_MAX;
    if (snX[idx] == cx && snY[idx] == cy) return true;
  }
  return false;
}

static void placeFood(int &fx, int &fy) {
  for (int tries = 0; tries < 400; tries++) {
    int cx = esp_random() % GRID_N;
    int cy = esp_random() % GRID_N;
    if (!cellValid(cx, cy)) continue;
    if (onSnake(cx, cy, 0)) continue;
    fx = cx;
    fy = cy;
    return;
  }
  fx = GRID_N / 2;
  fy = 1;
}

int playSnake() {
  int best = prefs.getUInt("bestSnake", 0);

  if (DEBUG_USB) Serial.println("Snake start");
  tft.fillScreen(C_BLACK);
  tft.drawCircle(120, 120, 119, C_RINGDIM);
  drawHud(0, best);

  // start in the middle, heading right
  snLen = SNAKE_START;
  snHead = 0;
  int cx = GRID_N / 2, cy = GRID_N / 2;
  for (int i = 0; i < snLen; i++) {
    int idx = (snHead - i + SNAKE_MAX) % SNAKE_MAX;
    snX[idx] = cx - i;
    snY[idx] = cy;
    paintCell(snX[idx], snY[idx], i == 0 ? C_PLAYER : C_GAME);
  }

  int dx = 1, dy = 0;          // current direction
  int wantX = 1, wantY = 0;    // direction requested by tilt
  int fx, fy;
  placeFood(fx, fy);
  paintCell(fx, fy, C_FOOD);

  int score = 0, shownScore = -1;
  int stepMs = STEP_START_MS;
  uint32_t lastStep = millis();
  uint32_t startMs = millis();
  uint32_t lastMoveMs = millis();
  bool alive = true;

  while (alive && millis() - startMs < MAX_PLAY_MS) {
    // --- read tilt often, step on the clock ---
    float up, right;
    if (tiltBoth(up, right)) {
      if (fabsf(up) > IDLE_TILT_DEG || fabsf(right) > IDLE_TILT_DEG) lastMoveMs = millis();
      if (fabsf(up) >= fabsf(right)) {
        // inverted on purpose: tipping the top away steers DOWN in snake
        if (up > TURN_DEG && dy == 0)       { wantX = 0; wantY = 1; }
        else if (up < -TURN_DEG && dy == 0) { wantX = 0; wantY = -1; }
      } else {
        if (right > TURN_DEG && dx == 0)      { wantX = 1; wantY = 0; }
        else if (right < -TURN_DEG && dx == 0){ wantX = -1; wantY = 0; }
      }
    }

    if (shakeSeen()) break;                    // shake = quit to menu
    if (millis() - lastMoveMs > IDLE_ABORT_MS) {
      if (DEBUG_USB) Serial.println("Idle abort");
      break;
    }

    if (millis() - lastStep < (uint32_t)stepMs) {
      delay(8);
      continue;
    }
    lastStep = millis();

    dx = wantX;
    dy = wantY;
    int nx = snX[snHead] + dx;
    int ny = snY[snHead] + dy;

    wrapCell(nx, ny, dx, dy);                 // edges wrap, only self-hits kill
    if (!cellValid(nx, ny)) { alive = false; break; }

    bool grew = (nx == fx && ny == fy);
    if (onSnake(nx, ny, grew ? 0 : 1)) { alive = false; break; }

    // tail first, so the vacated cell is free
    if (!grew) {
      int tail = (snHead - snLen + 1 + SNAKE_MAX) % SNAKE_MAX;
      paintCell(snX[tail], snY[tail], C_BLACK);
    } else if (snLen < SNAKE_MAX) {
      snLen++;
    }

    // old head dims to body colour
    paintCell(snX[snHead], snY[snHead], C_GAME);

    snHead = (snHead + 1) % SNAKE_MAX;
    snX[snHead] = nx;
    snY[snHead] = ny;
    paintCell(nx, ny, C_PLAYER);

    if (grew) {
      score++;
      stepMs = STEP_START_MS - score * STEP_STEP_MS;
      if (stepMs < STEP_MIN_MS) stepMs = STEP_MIN_MS;
      placeFood(fx, fy);
      paintCell(fx, fy, C_FOOD);
    }

    if (score != shownScore) { drawHud(score, best); shownScore = score; }
  }

  // death flash on the head
  paintCell(snX[snHead], snY[snHead], C_DANGER);
  delay(300);
  return score;
}
