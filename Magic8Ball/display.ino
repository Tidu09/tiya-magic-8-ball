// =====================================================================
//  display.ino — display power, starfield, text helpers
// =====================================================================

void backlight(bool on) {
  if (TFT_BL < 0) return;
  digitalWrite(TFT_BL, on ? BL_ON : !BL_ON);
}

void displayOn() {
#if USE_HW_SPI
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);   // no MISO -> keeps GPIO5 free
  tft.begin(SPI_FREQ);
#else
  tft.begin();
#endif
  tft.setRotation(0);
  tft.setTextWrap(false);
  tft.fillScreen(C_BLACK);
  backlight(true);
}

void displaySleep() {
  tft.fillScreen(C_BLACK);
  backlight(false);
  tft.sendCommand(0x28);   // DISPOFF
  tft.sendCommand(0x10);   // SLPIN
  delay(120);
}

void holdBacklightOff() {
  if (TFT_BL < 0) return;
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, !BL_ON);
  gpio_hold_en((gpio_num_t)TFT_BL);
#if defined(SOC_GPIO_SUPPORT_HOLD_IO_IN_DSLP) && !defined(SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP)
  gpio_deep_sleep_hold_en();
#endif
}

// ---------------- Stars ----------------
void makeStars(int rMin, int rMax) {
  for (int i = 0; i < NUM_STARS; i++) {
    int16_t x, y;
    int d2;
    do {
      x = 120 + (int16_t)(esp_random() % (2 * rMax + 1)) - rMax;
      y = 120 + (int16_t)(esp_random() % (2 * rMax + 1)) - rMax;
      d2 = (x - 120) * (x - 120) + (y - 120) * (y - 120);
    } while (d2 < rMin * rMin || d2 > rMax * rMax);
    starX[i] = x;
    starY[i] = y;
    uint32_t roll = esp_random() % 10;
    starLvl[i] = (roll < 4) ? 0 : (roll < 7) ? 1 : (roll < 9) ? 2 : 3;
  }
}

void drawStar(int i, uint16_t bg) {
  int16_t x = starX[i], y = starY[i];
  uint16_t arm = (starLvl[i] == 3) ? STAR_COL[1] : bg;
  tft.drawPixel(x - 1, y, arm);
  tft.drawPixel(x + 1, y, arm);
  tft.drawPixel(x, y - 1, arm);
  tft.drawPixel(x, y + 1, arm);
  tft.drawPixel(x, y, STAR_COL[starLvl[i]]);
}

void drawAllStars(uint16_t bg) {
  for (int i = 0; i < NUM_STARS; i++) drawStar(i, bg);
}

void twinkle(int count, uint16_t bg) {
  for (int k = 0; k < count; k++) {
    int i = esp_random() % NUM_STARS;
    starLvl[i] = esp_random() % 4;
    drawStar(i, bg);
  }
}

// ---------------- Text ----------------
uint16_t textWidth(const char* s) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void drawCentered(const char* s, int16_t cy, const GFXfont* f, uint8_t size, uint16_t color) {
  tft.setFont(f);
  tft.setTextSize(size);
  tft.setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(120 - (int16_t)w / 2 - x1, cy - (int16_t)h / 2 - y1);
  tft.print(s);
}

// Bold heading text, biggest size that fits maxW; '\n' splits lines
void drawMultiline(const char* text, int16_t cy, uint16_t maxW, uint16_t color) {
  char buf[48];
  strncpy(buf, text, sizeof(buf));
  buf[sizeof(buf) - 1] = 0;

  char* lines[4];
  int n = 0;
  char* p = strtok(buf, "\n");
  while (p && n < 4) { lines[n++] = p; p = strtok(NULL, "\n"); }
  if (n == 0) return;

  const GFXfont* f = FONTS[NUM_FONTS - 1];
  for (int k = 0; k < NUM_FONTS; k++) {
    tft.setFont(FONTS[k]);
    tft.setTextSize(1);
    uint16_t widest = 0;
    for (int i = 0; i < n; i++) {
      uint16_t w = textWidth(lines[i]);
      if (w > widest) widest = w;
    }
    if (widest <= maxW && (uint16_t)(n * FONTS[k]->yAdvance) <= maxW) {
      f = FONTS[k];
      break;
    }
  }

  int16_t lh = f->yAdvance;
  int16_t top = cy - (n * lh) / 2;
  for (int i = 0; i < n; i++) {
    drawCentered(lines[i], top + lh * i + lh / 2, f, 1, color);
  }
}

// ---------------- Triangle text layout ----------------
float triAvail(int y) {
  if (y < TRI_TOP) y = TRI_TOP;
  if (y >= TRI_APEX) return 0;
  return 2.0f * TRI_HALF * (TRI_APEX - y) / (float)(TRI_APEX - TRI_TOP) - 2 * TRI_PAD;
}

bool layoutTriangle(char words[][16], int nw, const GFXfont* f, int y0, int maxLines,
                    char lines[][40], int &n) {
  tft.setFont(f);
  tft.setTextSize(1);
  int lh = f->yAdvance;
  n = 0;
  int i = 0;
  while (i < nw) {
    if (n >= maxLines) return false;
    int bottom = y0 + (n + 1) * lh - lh / 5;
    float avail = triAvail(bottom);

    char cur[40];
    strncpy(cur, words[i], sizeof(cur));
    cur[sizeof(cur) - 1] = 0;
    if (textWidth(cur) > avail) return false;
    i++;

    while (i < nw) {
      char tryBuf[40];
      snprintf(tryBuf, sizeof(tryBuf), "%s %s", cur, words[i]);
      if (textWidth(tryBuf) > avail) break;
      strcpy(cur, tryBuf);
      i++;
    }
    strcpy(lines[n++], cur);
  }
  return true;
}

// One fixed font; block stays centred, nudged up only as much as it must
void drawInTriangle(const char* text, uint16_t color) {
  char buf[64];
  strncpy(buf, text, sizeof(buf));
  buf[sizeof(buf) - 1] = 0;

  char words[MAX_WORDS][16];
  int nw = 0;
  for (char* p = strtok(buf, " \n"); p && nw < MAX_WORDS; p = strtok(NULL, " \n")) {
    strncpy(words[nw], p, 15);
    words[nw][15] = 0;
    nw++;
  }
  if (nw == 0) return;

  const GFXfont* f = ANSWER_FONT;
  const int lh = f->yAdvance;
  const int topY = TRI_TOP + 6;
  char lines[MAX_TLINES][40];
  int n = 0;

  for (int dy = 0; dy <= 60; dy += 2) {
    for (int L = 1; L <= MAX_TLINES; L++) {
      int y0 = TRI_TEXT_CY - (L * lh) / 2 - dy;
      if (y0 < topY) continue;
      if (layoutTriangle(words, nw, f, y0, L, lines, n) && n == L) {
        for (int i = 0; i < n; i++) {
          drawCentered(lines[i], y0 + i * lh + lh / 2, f, 1, color);
        }
        return;
      }
    }
  }

  layoutTriangle(words, nw, f, topY, MAX_TLINES, lines, n);
  for (int i = 0; i < n; i++) {
    drawCentered(lines[i], topY + i * lh + lh / 2, f, 1, color);
  }
}

// ---------------- Shared game HUD ----------------
void drawHud(int score, int best) {
  tft.fillRect(60, 2, 120, 24, C_BLACK);
  char buf[28];
  snprintf(buf, sizeof(buf), "%d   best %d", score, best);
  drawCentered(buf, 14, &FreeSans9pt7b, 1, C_WHITE);
}
