// =====================================================================
//  beep_eyes.ino — Beep's eye styles, drawn into an off-screen canvas
//
//  Everything here only uses Adafruit_GFX calls, so the same code also
//  runs on a PC to render preview pictures. Canvas coordinates: the
//  canvas is BEEP_CV_W x BEEP_CV_H and sits at screen (BEEP_CV_X, BEEP_CV_Y).
//
//  Every helper is prefixed be_ because Arduino glues the tabs together.
// =====================================================================

#define BE_BG     0x0000
#define BE_WHITE  0xFFFF
#define BE_PINK   0xFB2E      // blush, rabbit ears, nose
#define BE_RED    0xF8A2
#define BE_BLUE   0x5D5F      // tears
#define BE_GOLD   0xFEA0
#define BE_HEARTC 0xFA31
#define BE_TAU    6.2831853f

static uint16_t be_blend(uint16_t a, uint16_t b, float f) {   // f=0 -> a, f=1 -> b
  if (f <= 0) return a;
  if (f >= 1) return b;
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + (int)((br - ar) * f + 0.5f);
  int g = ag + (int)((bg - ag) * f + 0.5f);
  int c = ab + (int)((bb - ab) * f + 0.5f);
  return (uint16_t)((r << 11) | (g << 5) | c);
}

// thick straight line: a row of filled circles
static void be_thickLine(Adafruit_GFX& g, float x0, float y0, float x1, float y1, int r, uint16_t c) {
  float dx = x1 - x0, dy = y1 - y0;
  int n = (int)(sqrtf(dx * dx + dy * dy) / 1.5f) + 1;
  for (int i = 0; i <= n; i++) {
    float k = (float)i / n;
    g.fillCircle((int)(x0 + dx * k + 0.5f), (int)(y0 + dy * k + 0.5f), r, c);
  }
}

static void be_oval(Adafruit_GFX& g, int cx, int cy, int w, int h, uint16_t c) {
  int r = w / 2;
  if (h < w) r = h / 2;
  if (h < 2) h = 2;
  g.fillRoundRect(cx - w / 2, cy - h / 2, w, h, r, c);
}

static void be_spiral(Adafruit_GFX& g, int cx, int cy, float maxR, float phase, int dir, uint16_t c) {
  for (int arm = 0; arm < 2; arm++) {
    for (int k = 2; k < 46; k++) {
      float rad = maxR * k / 46.0f;
      float ang = dir * (k * 0.30f + phase) + arm * 3.14159f;
      g.fillCircle(cx + (int)(rad * cosf(ang)), cy + (int)(rad * sinf(ang)), k < 12 ? 1 : 2, c);
    }
  }
}

static void be_heart(Adafruit_GFX& g, int cx, int cy, float s, uint16_t c) {
  int r = (int)(12 * s);
  g.fillCircle(cx - (int)(10 * s), cy - (int)(6 * s), r, c);
  g.fillCircle(cx + (int)(10 * s), cy - (int)(6 * s), r, c);
  g.fillTriangle(cx - (int)(21.5f * s), cy - (int)(1 * s), cx + (int)(21.5f * s), cy - (int)(1 * s),
                 cx, cy + (int)(24 * s), c);
}

static void be_star(Adafruit_GFX& g, int cx, int cy, float rOut, float rIn, float rot, uint16_t c) {
  float px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float r = (i & 1) ? rIn : rOut;
    float a = rot - 1.5708f + i * (BE_TAU / 10);
    px[i] = cx + r * cosf(a);
    py[i] = cy + r * sinf(a);
  }
  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;
    g.fillTriangle(cx, cy, (int)px[i], (int)py[i], (int)px[j], (int)py[j], c);
  }
}

// Height (half) of each style's eye, used by the blink lids. 0 = no blink.
static int be_blinkHalf(uint8_t s) {
  switch (s) {
    case BEEP_CHILL: case BEEP_ANGRY: case BEEP_CRY: return 25;
    case BEEP_FOCUSED: case BEEP_SIDEEYE:           return 25;
    case BEEP_CUTE:      return 26;
    case BEEP_RABBIT:    return 18;
    case BEEP_PIXEL:     return 26;
    case BEEP_SURPRISED: return 29;
    default:             return 0;
  }
}

// One eye. side = -1 (left) or +1 (right).
static void be_eye(Adafruit_GFX& g, const BeepFace& f, int ex, int cy, int side) {
  uint16_t C = f.color;
  uint32_t t = f.t;
  switch (f.style) {
    case BEEP_CHILL:
      be_oval(g, ex, cy, 30, 46, C);
      break;

    case BEEP_FOCUSED:                        // flat lid: determined
      be_oval(g, ex, cy + 2, 32, 44, C);
      g.fillRect(ex - 18, cy - 24, 36, 13, BE_BG);
      break;

    case BEEP_CUTE:
      g.fillCircle(ex, cy, 25, C);
      g.fillCircle(ex, cy + 2, 16, be_blend(C, BE_BG, 0.55f));
      g.fillCircle(ex - 9, cy - 9, 8, BE_WHITE);
      g.fillCircle(ex + 8, cy + 9, 3, BE_WHITE);
      g.fillCircle(ex + 12, cy - 2, 2, BE_WHITE);
      break;

    case BEEP_RABBIT:
      g.fillRoundRect(ex - 10, 0, 20, 34, 10, C);                 // ear
      g.fillRoundRect(ex - 4, 6, 8, 23, 4, BE_PINK);
      g.fillCircle(ex, cy + 4, 16, C);
      g.fillCircle(ex, cy + 6, 9, be_blend(C, BE_BG, 0.6f));
      g.fillCircle(ex - 6, cy - 2, 5, BE_WHITE);
      break;

    case BEEP_BLURRY: {                       // soft glowing orb, breathing
      float pulse = 1.0f + 0.08f * sinf(t * 0.004f + side);
      int R = (int)(34 * pulse);
      for (int r = R; r >= 4; r -= 2) {
        float k = 1.0f - (float)r / R;
        g.fillCircle(ex, cy, r, be_blend(BE_BG, C, powf(k, 0.9f) * 1.15f));
      }
      g.fillCircle(ex, cy, 6, be_blend(C, BE_WHITE, 0.6f));
      break;
    }

    case BEEP_HYPNO:
      g.drawCircle(ex, cy, 29, C);
      g.drawCircle(ex, cy, 28, C);
      be_spiral(g, ex, cy, 25, t * 0.009f, side, C);
      break;

    case BEEP_PIXEL: {                        // chunky retro eye, 8 px blocks
      for (int r = 0; r < 6; r++)
        for (int c = 0; c < 5; c++) {
          bool corner = (r == 0 || r == 5) && (c == 0 || c == 4);
          if (corner) continue;
          bool glint = (r == 1 && c == 1);
          g.fillRect(ex - 20 + c * 8, cy - 24 + r * 8, 7, 7, glint ? BE_WHITE : C);
        }
      break;
    }

    case BEEP_HEART: {
      float s = 1.0f + 0.10f * sinf(t * 0.008f);
      be_heart(g, ex, cy - 2, s, BE_HEARTC);
      g.fillCircle(ex - 9, cy - 10, 4, be_blend(BE_HEARTC, BE_WHITE, 0.7f));
      break;
    }

    case BEEP_STAR: {
      float tw = 1.0f + 0.10f * sinf(t * 0.010f + side * 1.3f);
      be_star(g, ex, cy, 28 * tw, 12 * tw, side * t * 0.0015f, BE_GOLD);
      break;
    }

    case BEEP_SLEEPY:                         // heavy, drooping lids
      be_oval(g, ex, cy + 4, 34, 40, C);
      g.fillRect(ex - 19, cy - 18, 38, 24, BE_BG);
      g.fillRect(ex - 19, cy + 6, 38, 4, be_blend(C, BE_WHITE, 0.35f));   // lid edge
      break;

    case BEEP_HAPPY:                          // ^ ^
      g.fillCircle(ex, cy + 6, 22, C);
      g.fillCircle(ex, cy + 15, 22, BE_BG);
      g.fillRect(ex - 24, cy + 4, 48, 30, BE_BG);
      break;

    case BEEP_SURPRISED:
      g.fillCircle(ex, cy, 29, C);
      g.fillCircle(ex, cy, 22, BE_BG);
      g.fillCircle(ex, cy, 11, C);
      g.fillCircle(ex - 4, cy - 4, 3, BE_WHITE);
      break;

    case BEEP_ANGRY: {
      uint16_t col = (f.anger >= 2) ? BE_RED : C;
      be_oval(g, ex, cy + 2, 32, 44, col);
      // brow: high at the outside, low in the middle  -> \ /
      g.fillTriangle(ex + side * 19, cy - 22, ex - side * 19, cy - 22, ex - side * 19, cy + 2, BE_BG);
      if (f.anger >= 3) g.fillRect(ex - 18, cy + 18, 36, 8, BE_BG);      // squeezed
      break;
    }

    case BEEP_SIDEEYE:                        // suspicious glance to the side
      be_oval(g, ex + 9, cy + 6, 26, 34, C);
      g.fillRect(ex - 16, cy - 20, 40, 17, BE_BG);
      g.fillRect(ex - 16, cy - 4, 40, 3, be_blend(C, BE_WHITE, 0.35f));
      break;

    case BEEP_DIZZY:
      be_spiral(g, ex, cy, 24, t * 0.016f, -side, BE_WHITE);
      break;

    case BEEP_CRY: {
      be_oval(g, ex, cy, 30, 44, C);
      // sad brow: high in the middle, low at the outside -> / \  (mirror of angry)
      g.fillTriangle(ex - side * 18, cy - 23, ex + side * 18, cy - 23, ex + side * 18, cy - 4, BE_BG);
      // tears falling from the outer corner
      for (int k = 0; k < 2; k++) {
        int ty = cy + 26 + (int)((t / 18 + k * 22 + (side > 0 ? 11 : 0)) % 44);
        int tx = ex + side * 12;
        if (ty < BEEP_CV_H - 6) {
          g.fillCircle(tx, ty, 4, BE_BLUE);
          g.fillTriangle(tx - 4, ty - 1, tx + 4, ty - 1, tx, ty - 9, BE_BLUE);
        }
      }
      break;
    }

    case BEEP_SQUINT:                         // > <  (too bright!)
      be_thickLine(g, ex + side * 14, cy - 15, ex - side * 12, cy, 3, C);
      be_thickLine(g, ex - side * 12, cy, ex + side * 14, cy + 15, 3, C);
      break;
  }
}

void beepDrawFace(Adafruit_GFX& g, const BeepFace& f) {
  g.fillScreen(BE_BG);
  int jx = 0, jy = 0;
  if (f.style == BEEP_ANGRY && f.anger >= 3) {      // full crash-out: shaking
    jx = (int)((f.t / 40) % 7) - 3;
    jy = (int)((f.t / 55) % 5) - 2;
  }
  int mid = BEEP_CV_W / 2 + f.lookX + jx;
  int cy = BEEP_EYE_CY + f.lookY + jy;

  for (int side = -1; side <= 1; side += 2) {
    int ex = mid + side * BEEP_EYE_DX;
    be_eye(g, f, ex, cy, side);

    int hh = be_blinkHalf(f.style);                 // blink: lids close from top and bottom
    if (hh > 0 && f.open < 0.99f) {
      int lid = (int)((1.0f - f.open) * (hh + 3));
      g.fillRect(ex - 34, cy - hh - 3, 68, lid, BE_BG);
      g.fillRect(ex - 34, cy + hh + 3 - lid, 68, lid, BE_BG);
    }
  }

  // extras around the eyes
  switch (f.style) {
    case BEEP_CUTE:
    case BEEP_HAPPY:
      g.fillRoundRect(mid - BEEP_EYE_DX - 30, cy + 30, 22, 9, 4, be_blend(BE_BG, BE_PINK, 0.6f));
      g.fillRoundRect(mid + BEEP_EYE_DX + 8,  cy + 30, 22, 9, 4, be_blend(BE_BG, BE_PINK, 0.6f));
      break;
    case BEEP_RABBIT:
      g.fillTriangle(mid - 6, cy + 22, mid + 6, cy + 22, mid, cy + 28, BE_PINK);   // nose
      g.drawCircle(mid - 5, cy + 31, 5, BE_PINK);                                  // w mouth
      g.drawCircle(mid + 5, cy + 31, 5, BE_PINK);
      g.fillRect(mid - 11, cy + 25, 22, 6, BE_BG);
      break;
    case BEEP_SLEEPY: {                         // z z z drifting up
      g.setFont(NULL);
      for (int k = 0; k < 3; k++) {
        int ph = (int)((f.t / 30 + k * 25) % 75);
        g.setTextSize(1 + k % 2);
        g.setTextColor(be_blend(f.color, BE_BG, ph / 90.0f));
        g.setCursor(mid + 52 + k * 6, 40 - ph / 2);
        g.print("z");
      }
      break;
    }
    case BEEP_ANGRY:
      if (f.anger >= 2) {                        // steam puffs
        int ph = (int)((f.t / 60) % 3);
        g.fillCircle(8 + ph * 2, 10 - ph * 2, 4 + ph, 0x7BEF);
        g.fillCircle(BEEP_CV_W - 10 - ph * 2, 10 - ph * 2, 4 + ph, 0x7BEF);
      }
      break;
    case BEEP_SURPRISED:
      g.fillCircle(mid, cy + 40, 6, f.color);
      g.fillCircle(mid, cy + 40, 3, BE_BG);
      break;
    default:
      break;
  }
}

// ---------------- screen furniture (also used by the previews) ----------------
static void be_text(Adafruit_GFX& g, const char* s, int cx, int cy, const GFXfont* f, uint16_t c) {
  g.setFont(f);
  g.setTextSize(1);
  g.setTextColor(c);
  int16_t x1, y1;
  uint16_t w, h;
  g.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  g.setCursor(cx - (int)w / 2 - x1, cy - (int)h / 2 - y1);
  g.print(s);
}

// ---- seven-segment clock, like a little LCD: unlit segments glow faintly ----
#define BE_SEG_W   32
#define BE_SEG_T   7
#define BE_SEG_V   20                // vertical segment length
#define BE_SEG_H   (3 * BE_SEG_T + 2 * BE_SEG_V)
#define BE_SEG_GAP 8
#define BE_COLON_W 16

static const uint8_t BE_DIGIT[10] = {   // bits: a b c d e f g
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

static void be_seg(Adafruit_GFX& g, int x, int y, int seg, uint16_t c) {
  const int t = BE_SEG_T, v = BE_SEG_V, w = BE_SEG_W;
  switch (seg) {
    case 0: g.fillRoundRect(x + t - 1, y,                 w - 2 * t + 2, t, 3, c); break;  // a
    case 1: g.fillRoundRect(x + w - t, y + t - 1,         t, v + 2, 3, c); break;          // b
    case 2: g.fillRoundRect(x + w - t, y + 2 * t + v - 1, t, v + 2, 3, c); break;          // c
    case 3: g.fillRoundRect(x + t - 1, y + 2 * t + 2 * v, w - 2 * t + 2, t, 3, c); break;  // d
    case 4: g.fillRoundRect(x,         y + 2 * t + v - 1, t, v + 2, 3, c); break;          // e
    case 5: g.fillRoundRect(x,         y + t - 1,         t, v + 2, 3, c); break;          // f
    case 6: g.fillRoundRect(x + t - 1, y + t + v,         w - 2 * t + 2, t, 3, c); break;  // g
  }
}

static void be_digit(Adafruit_GFX& g, int x, int y, int d, uint16_t on, uint16_t ghost) {
  for (int s = 0; s < 7; s++) be_seg(g, x, y, s, (BE_DIGIT[d] >> s) & 1 ? on : ghost);
}

// Whole canvas: m:ss (or mm:ss) in the middle
void beepDrawClock(Adafruit_GFX& g, long secs, uint16_t c) {
  g.fillScreen(BE_BG);
  if (secs < 0) secs = 0;
  int m = secs / 60, s = secs % 60;
  if (m > 99) m = 99;
  int digits[4], n = 0;
  if (m >= 10) digits[n++] = m / 10;
  digits[n++] = m % 10;
  digits[n++] = s / 10;
  digits[n++] = s % 10;
  int width = n * BE_SEG_W + (n - 2) * BE_SEG_GAP + BE_COLON_W;
  int x = (BEEP_CV_W - width) / 2;
  int y = (BEEP_CV_H - BE_SEG_H) / 2;
  uint16_t ghost = be_blend(BE_BG, c, 0.07f);
  for (int i = 0; i < n; i++) {
    if (i == n - 2) {                                   // the colon
      int cx = x + BE_COLON_W / 2 - 1;
      g.fillCircle(cx, y + BE_SEG_H / 3, 4, c);
      g.fillCircle(cx, y + 2 * BE_SEG_H / 3, 4, c);
      x += BE_COLON_W;
    }
    be_digit(g, x, y, digits[i], c, ghost);
    x += BE_SEG_W + (i < n - 1 && i != n - 3 ? BE_SEG_GAP : 0);
  }
}
