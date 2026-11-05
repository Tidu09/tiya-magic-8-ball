/*
 * Magic 8-Ball keychain + tilt games
 * ESP32-C3 SuperMini + LIS3DH + GC9A01 240x240 round TFT
 *
 * Shake -> "Hi Tiya!" greeting
 *            held flat/upright  -> 8-ball answer (default)
 *            held edge-on (>60) -> calibrate -> game menu
 *                                  (last-but-one menu row: "Update" over Wi-Fi)
 *
 * FIRST USB UPLOAD: Tools -> Partition Scheme ->
 *   "Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)"
 * Libraries: Adafruit GFX, Adafruit GC9A01A, WiFiManager (by tzapu)
 *
 * Wiring:
 *   LIS3DH : SDA=8, SCL=9, INT1=5, addr 0x19
 *   GC9A01 : MOSI(SDA)=7, SCLK(SCL)=6, CS=10, DC=4, RST=3, BLK=1
 *
 * TABS: this file holds every #define, struct and global.
 *       The other tabs hold only functions.
 */
#include <Wire.h>
#include <SPI.h>
#include <math.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiManager.h>          // Library Manager: "WiFiManager" by tzapu
#include "esp_sleep.h"
#include "esp_random.h"
#include "driver/gpio.h"
#include "soc/soc_caps.h"

// ---------------- Pins ----------------
#define I2C_SDA   8
#define I2C_SCL   9
#define WAKE_PIN  5
#define LIS_ADDR  0x19

#define TFT_MOSI  7
#define TFT_SCLK  6
#define TFT_CS    10
#define TFT_DC    4
#define TFT_RST   3
#define TFT_BL    1
#define BL_ON     HIGH

#define USE_HW_SPI 1
#define SPI_FREQ   40000000

#define DEBUG_USB  1

// ---------------- Firmware version + over-the-air update ----------------
// Bump FW_VERSION and push to GitHub to ship an update: the repo's Actions
// workflow compiles it and publishes firmware/ only when this number goes up.
#define FW_VERSION     3
#define OTA_BASE_URL   "https://raw.githubusercontent.com/Tidu09/tiya-magic-8-ball/main/firmware/"
#define OTA_AP_NAME    "Tiya-8Ball"      // setup hotspot name
#define OTA_CONNECT_S  15                // seconds to try the saved Wi-Fi
#define OTA_PORTAL_S   240               // seconds the setup hotspot stays up
// C3 SuperMini boards brown out / reset at full Wi-Fi power on a small
// battery. 8.5 dBm is the well-known safe value and still fine at home range.
#define OTA_TX_POWER   WIFI_POWER_8_5dBm

// ---------------- Timing ----------------
#define GREETING_MS   900
#define LOADER_MS     1700
#define ANSWER_MS     3000
#define EIGHTBALL_MS  1200

// ---------------- Colours ----------------
#define C_BLACK    0x0000
#define C_WHITE    0xFFFF
#define C_YELLOW   0xFFE0
#define C_NAVY     0x0005
#define C_TRI      0x3295
#define C_TRI_EDGE 0x6C3A
#define C_SP1      0x65FA
#define C_SP2      0x3C19
#define C_SP3      0x2232
#define C_EIGHT    0x96FF
#define C_RINGDIM  0x4208
#define C_GAME     0x07FF   // cyan: walls, snake body, maze rings
#define C_PLAYER   0xFFE0
#define C_DANGER   0xF9A6
#define C_FOOD     0xFD20
#define C_DIMTEXT  0xA534

// Star brightness levels (dim -> bright)
const uint16_t STAR_COL[4] = {0x2945, 0x5AEB, 0x9CF3, 0xDEFB};

// ---------------- Triangle geometry ----------------
#define TRI_R        104
#define TRI_HALF     90
#define TRI_TOP      (120 - TRI_R / 2)
#define TRI_APEX     (120 + TRI_R)
#define TRI_PAD      6
#define TRI_TEXT_CY  116
#define MAX_WORDS    8
#define MAX_TLINES   4
#define ANSWER_FONT  (&FreeSans12pt7b)
#define NUM_STARS    70

// ---------------- LIS3DH registers ----------------
#define REG_WHO_AM_I   0x0F
#define REG_CTRL1      0x20
#define REG_CTRL2      0x21
#define REG_CTRL3      0x22
#define REG_CTRL4      0x23
#define REG_CTRL5      0x24
#define REG_CTRL6      0x25
#define REG_REFERENCE  0x26
#define REG_STATUS     0x27
#define REG_OUT_X_L    0x28
#define REG_INT1_CFG   0x30
#define REG_INT1_SRC   0x31
#define REG_INT1_THS   0x32
#define REG_INT1_DUR   0x33
#define REG_CLICK_CFG  0x38

// ================= SHAKE TUNING =================
#define WAKE_THS      0x40    // ~2.05 g (+-4 g range, HP-filtered)
#define WAKE_DUR      0x02

#define CONFIRM_MS      1500
#define REV_G           2.0f
#define PEAKS_NEEDED    3
#define MIN_GAP_MS      60
#define MAX_GAP_MS      350

#define STILL_MS        300

// ================= TILT TUNING =================
#define SCREEN_NORMAL_AXIS 2     // sensor axis perpendicular to the screen
#define SCREEN_UP_AXIS     0     // sensor axis toward the TOP of the screen
#define SCREEN_UP_SIGN     1     // flip to -1 if up/down is reversed
#define SCREEN_RIGHT_AXIS  1     // sensor axis toward the RIGHT of the screen
#define SCREEN_RIGHT_SIGN  -1     // flip to -1 if left/right is reversed

#define GAME_TILT_COS      0.50f // cos(60 deg): below this = edge-on = games
#define TILT_FULL_DEG      25.0f // tilt for full deflection (runner)
#define CAL_TOTAL_MS       2000
#define CAL_USE_MS         1000
#define CAL_BAD_DOT        0.93f

// ================= SHARED GAME LIMITS =================
#define FRAME_MS        25
#define MAX_PLAY_MS     90000UL
#define IDLE_ABORT_MS   15000UL
#define IDLE_TILT_DEG   3.0f

// ================= RUNNER =================
#define PLAY_TOP      28
#define PLAY_BOT      236
#define PLAY_MID      132
#define PLAYER_X      60
#define PLAYER_R      7
#define WALL_W        12
#define MAX_WALLS     3
#define WALL_SPACING  120
#define SPEED_START   2.2f
#define SPEED_STEP    0.12f
#define SPEED_MAX     7.0f
#define GAP_START     96
#define GAP_STEP      2.2f
#define GAP_MIN       48

// ================= SNAKE =================
#define CELL          16
#define GRID_N        13                 // 13 x 13 cells
#define GRID_ORIGIN   16                 // pixel of cell 0
#define GRID_RADIUS   96                 // cells whose centre is farther out are wall
#define SNAKE_MAX     90
#define SNAKE_START   4
#define STEP_START_MS 320
#define STEP_STEP_MS  12
#define STEP_MIN_MS   140
#define TURN_DEG      14.0f              // tilt needed to set a direction

// ================= MARBLE MAZE =================
#define MAZE_RINGS      3
#define MAZE_BALL_R     6
#define MAZE_OUTER_R    112
#define MAZE_GOAL_R     16
#define MAZE_ACCEL      0.014f           // px per frame^2 per degree of tilt
#define MAZE_FRICTION   0.975f
#define MAZE_VMAX       6.0f
#define MAZE_BOUNCE     0.35f
#define MAZE_TIME_MS    60000UL
#define MAZE_GAP_START  0.75f            // gap half-width, radians (~86 deg opening)
#define MAZE_GAP_STEP   0.05f

// ================= Platformer =================

#define PF_JUMP_G     0.28f   // jerk this hard (g off rest) to jump. LOWER = more sensitive
#define PF_JUMP_REARM 0.14f   // must settle back under this before the next jump
#define PF_JUMP_GAP   110     // ms lockout after a jump, swallows the rebound
#define PF_JUMP_DIR   1       // 1 = up jerk only, -1 = flip, 0 = any direction

// ================= IN-GAME SHAKE (exit gesture) =================
#define SHAKE_MAG       1.90f    // g, on the +-2 g tilt range
#define SHAKE_HITS      6
#define SHAKE_WIN_MS    700
#define SHAKE_MIN_GAP   50

// ================= MENU =================
#define MENU_FLICK_G    0.55f    // jerk this hard (g, off the resting value) to advance
#define MENU_REARM_G    0.22f    // must settle back under this before the next flick
#define MENU_FLICK_GAP  180      // ms lockout after a flick, kills the rebound
#define MENU_PICK_DEG   16.0f
#define MENU_HOLD_MS    800
#define MENU_TIMEOUT_MS 20000UL

// ---------------- Types ----------------
struct ShakeResult {
  int peaks;
  float maxDev;
};

struct GameEntry {
  const char* name;
  const char* key;     // NVS key for this game's high score
};

// ---------------- Answers ----------------
const char* const ANSWERS[] = {
  "Yes",
  "No",
  "Without a doubt",
  "Don't count on it",
  "Just do it",
  "Maybe",
  "Suspiciously good odds",
  "Outlook not so good",
  "Most likely",
  "Cannot predict now",
  "Try again later",
  "Absolutely!",
};
const int NUM_ANSWERS = sizeof(ANSWERS) / sizeof(ANSWERS[0]);

// ---------------- Game list ----------------
const GameEntry GAMES[] = {
  {"Tilt runner", "bestRun"},
  {"Roulette",    "bestRoul"},
  {"Snake",       "bestSnake"},
  {"Marble maze", "bestMaze"},
  {"Platformer",  "bestPlat"},
};
const int NUM_GAMES = sizeof(GAMES) / sizeof(GAMES[0]);

const GFXfont* const FONTS[] = {
  &FreeSansBold24pt7b, &FreeSansBold18pt7b, &FreeSansBold12pt7b, &FreeSansBold9pt7b
};
const int NUM_FONTS = sizeof(FONTS) / sizeof(FONTS[0]);

// ---------------- Globals ----------------
#if USE_HW_SPI
Adafruit_GC9A01A tft(&SPI, TFT_DC, TFT_CS, TFT_RST);
#else
Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
#endif

Preferences prefs;

RTC_DATA_ATTR int8_t lastAnswer = -1;

int16_t starX[NUM_STARS], starY[NUM_STARS];
uint8_t starLvl[NUM_STARS];

float accelScale = 0.004f;      // g per LSB for the current range
float refVec[3] = {0, 0, 1};    // calibration reference (unit gravity)
float calUp = 0, calRight = 0;  // reference pitch/roll in degrees
bool gShakeSeen = false;        // set by the in-game shake detector

// ---------------- Forward declarations (tabs are one big file) ----------------
// sensor
bool writeReg(uint8_t reg, uint8_t val);
uint8_t readReg(uint8_t reg);
bool readAccelG(float a[3]);
bool sampleAccel(float a[3]);
void configureWakeOnShake();
void configureTiltMode();
ShakeResult confirmShake();
bool waitForStill(uint32_t timeoutMs);
bool wantsGame(uint32_t windowMs);
void calibrate();
bool tiltBoth(float &up, float &right);
float tiltUpDeg();
void screenAngles(const float a[3], float &u, float &r);
void shakeReset();
bool shakeSeen();
bool shakePoll();
bool menuFlick();
// display
void backlight(bool on);
void displayOn();
void displaySleep();
void holdBacklightOff();
void makeStars(int rMin, int rMax);
void drawStar(int i, uint16_t bg);
void drawAllStars(uint16_t bg);
void twinkle(int count, uint16_t bg);
uint16_t textWidth(const char* s);
void drawCentered(const char* s, int16_t cy, const GFXfont* f, uint8_t size, uint16_t color);
void drawMultiline(const char* text, int16_t cy, uint16_t maxW, uint16_t color);
void drawInTriangle(const char* text, uint16_t color);
void drawHud(int score, int best);
// fortune
void drawGreeting();
void galaxySpin(uint32_t ms);
void drawAnswer(const char* text);
void drawEightBall();
int  pickAnswer();
void runFortune();
// menu
int  showMenu();
void showGameOver(const char* name, int score, int best, bool newBest);
// games
int playRunner();
int playSnake();
int playMaze();
int playPlatform();
int playRoulette();
// ota
void runUpdate();

// =====================================================================
//                              MAIN
// =====================================================================
void runGameFlow() {
  tft.fillScreen(C_BLACK);
  drawMultiline("Games", 120, 170, C_YELLOW);
  delay(500);
  calibrate();

  while (true) {
    int sel = showMenu();
    if (DEBUG_USB) Serial.printf("Menu selection: %d\n", sel);
    if (sel < 0) return;                    // shake in the menu, or timeout
    if (sel == NUM_GAMES) {                 // "Update" row
      runUpdate();
      continue;
    }

    // play the chosen game over and over until a shake sends us back
    while (true) {
      int best = prefs.getUInt(GAMES[sel].key, 0);
      shakeReset();
      int score = 0;
      switch (sel) {
        case 0: score = playRunner();   break;
        case 1: score = playRoulette(); break;
        case 2: score = playSnake();    break;
        case 3: score = playMaze();     break;
        case 4: score = playPlatform(); break;
      }

      bool newBest = score > best;
      if (newBest) {
        best = score;
        prefs.putUInt(GAMES[sel].key, best);   // written only when it improves
      }
      showGameOver(GAMES[sel].name, score, best, newBest);
      if (shakeSeen()) break;               // shake -> back to the menu
    }
  }
}

void setup() {
  setCpuFrequencyMhz(80);            // ~1/3 less MCU current; SPI still 40 MHz

  if (TFT_BL >= 0) {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, !BL_ON);
    gpio_hold_dis((gpio_num_t)TFT_BL);
  }

  pinMode(WAKE_PIN, INPUT_PULLDOWN);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  bool gpioWake  = (cause == ESP_SLEEP_WAKEUP_GPIO);
  bool timerWake = (cause == ESP_SLEEP_WAKEUP_TIMER);
  bool coldBoot  = !gpioWake && !timerWake;

  // Measure the shake FIRST (no delays before this)
  uint8_t src = 0;
  ShakeResult r;
  r.peaks = 0;
  r.maxDev = 0.0f;
  bool confirmed = false;
  if (gpioWake) {
    src = readReg(REG_INT1_SRC);
    r = confirmShake();
    confirmed = (r.peaks >= PEAKS_NEEDED);
  }

  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  if (DEBUG_USB && coldBoot) delay(1500);

  uint8_t who = readReg(REG_WHO_AM_I);
  if (who != 0x33) {
    Serial.printf("LIS3DH not found (WHO_AM_I=0x%02X)\n", who);
    Serial.flush();
    holdBacklightOff();
    esp_deep_sleep(10ULL * 1000000ULL);
  }

  prefs.begin("m8b", false);

  if (gpioWake) {
    if (DEBUG_USB) Serial.printf("Wake: INT1_SRC=0x%02X reversals=%d maxDev=%.2f g -> %s\n",
                                 src, r.peaks, r.maxDev, confirmed ? "SHAKE" : "rejected");
    if (confirmed) {
      displayOn();
      drawGreeting();
      configureTiltMode();
      bool game = wantsGame(GREETING_MS);
      if (game) runGameFlow();
      else      runFortune();
      displaySleep();
    }
  } else if (coldBoot) {
    // Why did we start? Shown small under the 8-ball so a surprise
    // "switch off" (really a reset) can be diagnosed without a cable.
    esp_reset_reason_t why = esp_reset_reason();
    const char* whyTxt =
        (why == ESP_RST_BROWNOUT) ? "power dip reset" :
        (why == ESP_RST_PANIC)    ? "crash reset" :
        (why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT) ? "watchdog reset" :
        (why == ESP_RST_SW)       ? "restarted" : NULL;
    if (DEBUG_USB) Serial.printf("Cold boot: firmware v%d, reset reason %d (%s)\n",
                                 FW_VERSION, (int)why, whyTxt ? whyTxt : "power on");
    displayOn();
    drawEightBall();
    char ver[32];
    if (whyTxt) snprintf(ver, sizeof(ver), "v%d  %s", FW_VERSION, whyTxt);
    else        snprintf(ver, sizeof(ver), "v%d", FW_VERSION);
    drawCentered(ver, 222, &FreeSans9pt7b, 1,
                 (whyTxt && why != ESP_RST_SW) ? C_DANGER : C_RINGDIM);
    delay(whyTxt ? 4000 : EIGHTBALL_MS);           // linger if something odd happened
    displaySleep();
  }

  prefs.end();
  configureWakeOnShake();

  if (waitForStill(5000) && digitalRead(WAKE_PIN) == LOW) {
    gpio_pullup_dis(GPIO_NUM_5);
    gpio_pulldown_en(GPIO_NUM_5);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKE_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);
    if (DEBUG_USB) Serial.println("Deep sleep. Shake me!");
  } else {
    esp_sleep_enable_timer_wakeup(5ULL * 1000000ULL);
    if (DEBUG_USB) Serial.println("Not still; timer nap 5 s");
  }

  holdBacklightOff();
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {}
