// =====================================================================
//  menu.ino — game picker + shared game-over screen
//
//  Rows: every game, then "Update" (over Wi-Fi), then "Sleep"
//  Flick UP         : advance one entry (a quick jerk, not a tilt)
//  Hold SIDEWAYS    : start the highlighted game
//  Shake            : leave, back to sleep
//  Nothing for 20 s : leave, back to sleep
// =====================================================================
#define MENU_ROWS   (NUM_GAMES + 2)
#define MENU_UPDATE NUM_GAMES       // row index of "Update"
#define MENU_SLEEP  (NUM_GAMES + 1) // row index of "Sleep"
#define MENU_VIS  4                 // rows that fit on the round screen; list scrolls

static void drawMenuList(int sel) {
  tft.fillRect(20, 54, 200, 156, C_BLACK);
  int shown = (MENU_ROWS < MENU_VIS) ? MENU_ROWS : MENU_VIS;
  int first = sel - shown / 2;                       // keep the selection in view
  if (first > MENU_ROWS - shown) first = MENU_ROWS - shown;
  if (first < 0) first = 0;
  if (first > 0)                                     // more above
    tft.fillTriangle(114, 60, 126, 60, 120, 54, C_RINGDIM);
  if (first + shown < MENU_ROWS)                     // more below
    tft.fillTriangle(114, 200, 126, 200, 120, 206, C_RINGDIM);

  for (int slot = 0; slot < shown; slot++) {
    int i = first + slot;
    int y = 80 + slot * 34;
    bool on = (i == sel);
    bool isSleep = (i == MENU_SLEEP);
    bool isUpdate = (i == MENU_UPDATE);

    if (on) {
      tft.fillRoundRect(34, y - 15, 172, 30, 7, 0x18C7);
      tft.drawRoundRect(34, y - 15, 172, 30, 7, isSleep ? C_DANGER : C_YELLOW);
    }

    if (isSleep) {
      drawCentered("Sleep", y + 1, &FreeSans9pt7b, 1, on ? C_DANGER : C_DIMTEXT);
    } else if (isUpdate) {
      drawCentered("Update", y - 3, &FreeSans9pt7b, 1, on ? C_WHITE : C_DIMTEXT);
      char b[20];
      snprintf(b, sizeof(b), "version %d", FW_VERSION);
      tft.setFont(NULL);
      tft.setTextSize(1);
      tft.setTextColor(on ? C_GAME : C_RINGDIM);
      tft.setCursor(120 - (int)(strlen(b) * 6 - 1) / 2, y + 5);
      tft.print(b);
    } else {
      drawCentered(GAMES[i].name, y - 3, &FreeSans9pt7b, 1, on ? C_WHITE : C_DIMTEXT);
      char b[20];
      snprintf(b, sizeof(b), "best %u", prefs.getUInt(GAMES[i].key, 0));
      tft.setFont(NULL);                       // tiny built-in font
      tft.setTextSize(1);
      tft.setTextColor(on ? C_YELLOW : C_RINGDIM);
      int16_t x1, y1; uint16_t w, h;
      tft.getTextBounds(b, 0, 0, &x1, &y1, &w, &h);
      tft.setCursor(120 - w / 2, y + 5);
      tft.print(b);
    }
  }
}
// Progress ring while the start tilt is held
static void drawHoldRing(float frac, uint16_t color) {
  static int lastDots = 0;
  int dots = (int)(frac * 36);
  if (dots == lastDots) return;
  if (dots < lastDots) {
    for (int i = 0; i < 36; i++) {
      float ang = i * 2.0f * PI / 36 - PI / 2.0f;
      tft.fillCircle(120 + (int)(110 * cosf(ang)), 120 + (int)(110 * sinf(ang)), 3, C_BLACK);
    }
  }
  for (int i = 0; i < dots; i++) {
    float ang = i * 2.0f * PI / 36 - PI / 2.0f;
    tft.fillCircle(120 + (int)(110 * cosf(ang)), 120 + (int)(110 * sinf(ang)), 3, color);
  }
  lastDots = dots;
}

// Returns the chosen game index, MENU_UPDATE for "Update", or -1 on
// shake / timeout / "Sleep"
int showMenu() {
  tft.fillScreen(C_BLACK);
  drawCentered("flick to change", 38, &FreeSans9pt7b, 1, C_DIMTEXT);
  drawCentered("hold sideways", 214, &FreeSans9pt7b, 1, C_RINGDIM);

  int sel = 0;
  drawMenuList(sel);
  shakeReset();

  uint32_t holdStart = 0;
  bool holding = false;
  uint32_t start = millis();

  while (millis() - start < MENU_TIMEOUT_MS) {
    float up, right;
    tiltBoth(up, right);

    if (shakeSeen()) return -1;                 // shake in the menu = sleep

    // one flick of the wrist = one entry down the list (wraps)
         if (menuFlick()) {
      sel = (sel + 1) % MENU_ROWS;
      drawMenuList(sel);
      start = millis();
    }

    // start: hold sideways, either direction
    bool sideways = fabsf(right) > MENU_PICK_DEG;
    if (sideways && holding) {
      float frac = (millis() - holdStart) / (float)MENU_HOLD_MS;
      if (frac >= 1.0f) {
        drawHoldRing(0, C_BLACK);
        return (sel == MENU_SLEEP) ? -1 : sel;
      }
      drawHoldRing(frac, C_YELLOW);
      start = millis();
    } else if (sideways) {
      holding = true;
      holdStart = millis();
    } else {
      if (holding) drawHoldRing(0, C_BLACK);
      holding = false;
    }

    delay(FRAME_MS);
  }
  return -1;
}

// Shown after every game. Auto-restarts unless the player shakes.
void showGameOver(const char* name, int score, int best, bool newBest) {
  delay(300);
  tft.fillScreen(C_BLACK);
  char b1[24], b2[24];
  snprintf(b1, sizeof(b1), "Score %d", score);
  snprintf(b2, sizeof(b2), "Best %d", best);
  drawMultiline(newBest ? "New best!" : "Game over", 80, 170, newBest ? C_YELLOW : C_WHITE);
  drawCentered(b1, 126, &FreeSans12pt7b, 1, C_WHITE);
  drawCentered(b2, 154, &FreeSans9pt7b, 1, C_RINGDIM);
  drawCentered("shake for menu", 190, &FreeSans9pt7b, 1, C_DIMTEXT);
  if (DEBUG_USB) Serial.printf("%s over: score=%d best=%d\n", name, score, best);

  uint32_t t0 = millis();
  while (millis() - t0 < 2200) {
    if (shakePoll()) return;                    // straight back to the menu
    delay(20);
  }

  // restarting the same game
  tft.fillScreen(C_BLACK);
  drawMultiline("Ready...", 120, 170, C_YELLOW);
  uint32_t t1 = millis();
  while (millis() - t1 < 700) {
    if (shakePoll()) return;
    delay(20);
  }
}
