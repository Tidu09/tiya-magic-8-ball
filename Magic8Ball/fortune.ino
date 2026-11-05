// =====================================================================
//  fortune.ino — the Magic 8-Ball flow
// =====================================================================

void drawGreeting() {
  tft.fillScreen(C_BLACK);
  drawMultiline("Hi Tiya!", 102, 160, C_YELLOW);
  drawMultiline("Let's see...", 146, 150, C_WHITE);
}

// Starry galaxy: muted spiral of dots around a ringed "8"
void galaxySpin(uint32_t ms) {
  const int ARMS = 3, DOTS = 18, TOT = ARMS * DOTS;
  const float R_IN = 34.0f, R_OUT = 110.0f, SWIRL = 3.4f;

  int16_t ox[TOT], oy[TOT];
  uint8_t orad[TOT];
  bool have = false;

  tft.fillScreen(C_NAVY);
  makeStars(30, 115);
  drawAllStars(C_NAVY);
  tft.drawCircle(120, 120, 118, C_RINGDIM);
  tft.drawCircle(120, 120, 25, C_SP2);
  drawCentered("8", 120, &FreeSansBold18pt7b, 1, C_EIGHT);

  float rot = 0.0f;
  uint32_t start = millis();
  while (millis() - start < ms) {
    int16_t nx[TOT], ny[TOT];
    uint8_t nrad[TOT];
    uint16_t ncol[TOT];

    int idx = 0;
    for (int a = 0; a < ARMS; a++) {
      for (int i = 0; i < DOTS; i++) {
        float t = i / (float)(DOTS - 1);
        float r = R_IN + t * (R_OUT - R_IN);
        float ang = rot + a * 2.0f * PI / ARMS + t * SWIRL;
        nx[idx] = 120 + (int16_t)(r * cosf(ang));
        ny[idx] = 120 + (int16_t)(r * sinf(ang));
        nrad[idx] = 1 + (uint8_t)(t * 1.6f);
        ncol[idx] = (t < 0.35f) ? C_SP1 : (t < 0.7f) ? C_SP2 : C_SP3;
        idx++;
      }
    }

    if (have) {
      for (int k = 0; k < TOT; k++) tft.fillCircle(ox[k], oy[k], orad[k], C_NAVY);
      drawAllStars(C_NAVY);
    }
    twinkle(4, C_NAVY);
    for (int k = 0; k < TOT; k++) {
      tft.fillCircle(nx[k], ny[k], nrad[k], ncol[k]);
      ox[k] = nx[k];
      oy[k] = ny[k];
      orad[k] = nrad[k];
    }
    have = true;

    rot += 0.20f;
    delay(25);
  }
}

void drawAnswer(const char* text) {
  tft.fillScreen(C_BLACK);
  makeStars(0, 115);
  drawAllStars(C_BLACK);

  int16_t xl = 120 - TRI_HALF, xr = 120 + TRI_HALF;
  tft.fillTriangle(xl, TRI_TOP, xr, TRI_TOP, 120, TRI_APEX, C_TRI);
  tft.drawTriangle(xl, TRI_TOP, xr, TRI_TOP, 120, TRI_APEX, C_TRI_EDGE);

  drawInTriangle(text, C_WHITE);
}

void drawEightBall() {
  tft.fillScreen(C_BLACK);
  tft.fillCircle(120, 98, 56, C_WHITE);
  drawCentered("8", 98, &FreeSansBold24pt7b, 2, C_BLACK);
  drawMultiline("MAGIC 8-BALL!", 190, 165, C_YELLOW);
}

int pickAnswer() {
  int i;
  do { i = esp_random() % NUM_ANSWERS; } while (i == lastAnswer && NUM_ANSWERS > 1);
  lastAnswer = i;
  return i;
}

void runFortune() {
  galaxySpin(LOADER_MS);

  int idx = pickAnswer();
  drawAnswer(ANSWERS[idx]);
  if (DEBUG_USB) Serial.printf("Answer: %s\n", ANSWERS[idx]);
  delay(ANSWER_MS);

  drawEightBall();
  delay(EIGHTBALL_MS);
}
