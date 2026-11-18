// =====================================================================
//  beep_run.ino — Beep, the study buddy (Pomodoro + phone check)
//
//  Start: shake, then hold the keychain edge-on with its RIGHT edge down
//         during "Hi Tiya!" (left edge down = games, as before).
//         Also the first row of the game menu.
//
//  No words on the face, ever: only eyes, drawings and the clock.
//  1. HELLO  happy eyes; joins the saved Wi-Fi (the one set up for Update)
//            and the phone page's message relay, reads her settings.
//  3. FOCUS  big clock in the middle (eyes peek out now and then, and for
//            every reaction). If the page is linked and she leaves it or picks
//            the phone up: side-eye, then ANGRY and the timer pauses.
//            Each incident is a strike; strike 2 = red, strike 3+ = crash-out.
//  4. BREAK  phone allowed; idle moods; sleepy in the last minute.
//  5. DONE   hearts (stars for a new record, tears if she quit), report
//            to the page, Wi-Fi off, back to the menu / sleep.
//
//  Gestures:  wiggle up-down    -> next "cute" eyes (cute, rabbit, heart...)
//             wiggle side-side  -> next "trippy" eyes (hypno, glow, pixel...)
//             hard shake        -> dizzy;  hard shake again while dizzy = quit
//             lay it flat       -> "too bright!" squint (Beep hates light)
//  Eyes also change by themselves every 45-90 s, blink, and glance around.
//
//  Every name here is prefixed bp_ because Arduino glues the tabs together.
// =====================================================================

#define BP_HELLO 0
#define BP_FOCUS 2
#define BP_BREAK 3
#define BP_DONE  4

#define BP_MINT  0x3FF2
#define BP_RED   0xF8A2
#define BP_DIM   0x2104

static const uint8_t BP_VERT[]  = {BEEP_CUTE, BEEP_RABBIT, BEEP_HEART, BEEP_STAR, BEEP_HAPPY, BEEP_CHILL};
static const uint8_t BP_HORIZ[] = {BEEP_HYPNO, BEEP_BLURRY, BEEP_PIXEL, BEEP_SURPRISED, BEEP_SLEEPY, BEEP_FOCUSED};
static const char* const BP_MOOD[BEEP_NUM_STYLES] = {
  "chill", "cute", "rabbit", "blurry", "hypno", "pixel", "heart", "star", "sleepy",
  "happy", "surprised", "focused", "angry", "sideeye", "dizzy", "cry", "squint"
};

static GFXcanvas16* bp_cv = NULL;          // eyes
static BeepCfg bp_cfg;

// ---------------- phone link (MQTT) ----------------
static WiFiClient bp_net;
static PubSubClient bp_mq(bp_net);
static char bp_cid[24];
static bool bp_wifiEverOk = false, bp_wifiHooked = false;
static uint32_t bp_lastHb = 0, bp_lastMqTry = 0, bp_lastWifiTry = 0;
static char bp_hbVal = '0';
static bool bp_linked = false, bp_gotSettings = false, bp_stopCmd = false, bp_skipCmd = false;
static int bp_broker = 0;
static uint32_t bp_mqOkAt = 0;           // when the relay last connected

// ---------------- motion ----------------
static float bp_g[3], bp_s[3];             // fast / slow gravity estimates
static int8_t bp_ws[2];
static uint32_t bp_wt[2], bp_wLock = 0;
static int bp_wr[2];
static float bp_we[2];
static int bp_hardHits = 0;
static uint32_t bp_hardFirst = 0, bp_hardLast = 0;
static bool bp_flat = false;
static uint32_t bp_flatSince = 0;

// =====================================================================
//  Settings
// =====================================================================
static void bp_defaults() {
  bp_cfg.focusMin = prefs.getUShort("bF", 25);
  bp_cfg.breakMin = prefs.getUShort("bB", 5);
  bp_cfg.rounds   = prefs.getUChar("bR", 4);
  bp_cfg.color    = prefs.getUChar("bC", 0);
  bp_cfg.check    = prefs.getBool("bK", true);
  bp_cfg.note[0]  = 0;
  prefs.getString("bN", bp_cfg.note, sizeof(bp_cfg.note));
  if (bp_cfg.color >= BEEP_NUM_COLORS) bp_cfg.color = 0;
}

static void bp_saveCfg() {                  // only writes what changed (flash wear)
  if (prefs.getUShort("bF", 25) != bp_cfg.focusMin) prefs.putUShort("bF", bp_cfg.focusMin);
  if (prefs.getUShort("bB", 5)  != bp_cfg.breakMin) prefs.putUShort("bB", bp_cfg.breakMin);
  if (prefs.getUChar("bR", 4)   != bp_cfg.rounds)   prefs.putUChar("bR", bp_cfg.rounds);
  if (prefs.getUChar("bC", 0)   != bp_cfg.color)    prefs.putUChar("bC", bp_cfg.color);
  if (prefs.getBool("bK", true) != bp_cfg.check)    prefs.putBool("bK", bp_cfg.check);
  char old[44] = "";
  prefs.getString("bN", old, sizeof(old));
  if (strcmp(old, bp_cfg.note) != 0) prefs.putString("bN", bp_cfg.note);
}

static long bp_jint(const char* js, const char* key, long def) {
  char pat[20];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char* p = strstr(js, pat);
  if (!p) return def;
  p += strlen(pat);
  while (*p == ' ') p++;
  if (*p == 't') return 1;                  // true
  if (*p == 'f') return 0;                  // false
  return atol(p);
}

static void bp_jstr(const char* js, const char* key, char* out, int outLen) {
  char pat[20];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const char* p = strstr(js, pat);
  if (!p) return;                           // key absent: keep the old value
  p += strlen(pat);
  int n = 0;
  while (*p && *p != '"' && n < outLen - 1) {
    if (*p == '\\' && p[1]) {
      p++;
      if (*p == 'u') { p += (strlen(p) >= 5) ? 5 : strlen(p); out[n++] = '?'; continue; }
      out[n++] = (*p == 'n') ? ' ' : *p;
      p++;
      continue;
    }
    unsigned char ch = (unsigned char)*p++;
    if (ch >= 32 && ch < 127) out[n++] = (char)ch;   // the font is plain ASCII
  }
  out[n] = 0;
}

static long bp_clamp(long v, long lo, long hi) { return v < lo ? lo : v > hi ? hi : v; }

static void bp_parseSettings(const char* js) {
  bp_cfg.focusMin = bp_clamp(bp_jint(js, "focus", bp_cfg.focusMin), 1, 90);
  bp_cfg.breakMin = bp_clamp(bp_jint(js, "brk", bp_cfg.breakMin), 1, 30);
  bp_cfg.rounds   = bp_clamp(bp_jint(js, "rounds", bp_cfg.rounds), 1, 8);
  bp_cfg.color    = bp_clamp(bp_jint(js, "color", bp_cfg.color), 0, BEEP_NUM_COLORS - 1);
  bp_cfg.check    = bp_jint(js, "check", bp_cfg.check) != 0;
  bp_jstr(js, "note", bp_cfg.note, sizeof(bp_cfg.note));
  bp_saveCfg();
}

// =====================================================================
//  Network
// =====================================================================
static void bp_onMsg(char* topic, byte* payload, unsigned int len) {
  char buf[400];
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, payload, len);
  buf[len] = 0;
  const char* leaf = strrchr(topic, '/');
  leaf = leaf ? leaf + 1 : topic;

  if (!strcmp(leaf, "hb")) {
    bp_lastHb = millis();
    bp_hbVal = len ? buf[0] : '0';
    bp_linked = true;
  } else if (!strcmp(leaf, "settings")) {
    bp_parseSettings(buf);
    bp_gotSettings = true;
    if (DEBUG_USB) Serial.printf("Beep settings: %u/%u min x%u colour %u check %d note '%s'\n",
                                 bp_cfg.focusMin, bp_cfg.breakMin, bp_cfg.rounds,
                                 bp_cfg.color, bp_cfg.check, bp_cfg.note);
  } else if (!strcmp(leaf, "cmd")) {
    if (!strncmp(buf, "stop", 4)) bp_stopCmd = true;
    if (!strncmp(buf, "skip", 4)) bp_skipCmd = true;
  }
}

static void bp_wifiEvent(WiFiEvent_t e) {
  if (e == ARDUINO_EVENT_WIFI_STA_START) WiFi.setTxPower(OTA_TX_POWER);
}

static void bp_wifiStart() {
  if (!bp_wifiHooked) { WiFi.onEvent(bp_wifiEvent); bp_wifiHooked = true; }
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(OTA_TX_POWER);
  WiFi.setSleep(WIFI_PS_MAX_MODEM);         // radio naps between beacons: big battery saver
  WiFi.begin();                              // the network saved by the Update setup
  bp_lastWifiTry = millis();
}

static void bp_pubTopic(const char* leaf, const char* msg, bool retain) {
  if (!bp_mq.connected()) return;
  char t[64];
  snprintf(t, sizeof(t), "%s/%s", BEEP_TOPIC, leaf);
  bp_mq.publish(t, msg, retain);
}

static bool bp_mqConnect() {
  static const char* const hosts[2] = {BEEP_BROKER_1, BEEP_BROKER_2};
  for (int i = 0; i < 2; i++) {
    int h = (bp_broker + i) % 2;
    bp_mq.setServer(hosts[h], BEEP_BROKER_PORT);
    if (bp_mq.connect(bp_cid)) {
      bp_broker = h;
      char t[64];
      const char* leaves[3] = {"settings", "hb", "cmd"};
      for (int k = 0; k < 3; k++) {
        snprintf(t, sizeof(t), "%s/%s", BEEP_TOPIC, leaves[k]);
        bp_mq.subscribe(t);
      }
      bp_lastHb = millis();                  // grace after every (re)connect
      bp_mqOkAt = millis();
      if (DEBUG_USB) Serial.printf("Beep: relay %s connected\n", hosts[h]);
      return true;
    }
    if (DEBUG_USB) Serial.printf("Beep: relay %s failed (%d)\n", hosts[h], bp_mq.state());
  }
  return false;
}

static void bp_netTick(uint32_t now) {
  if (WiFi.status() != WL_CONNECTED) {
    if (bp_wifiEverOk && now - bp_lastWifiTry > 60000) {   // dropped: retry once a minute
      WiFi.reconnect();
      bp_lastWifiTry = now;
    }
    return;
  }
  bp_wifiEverOk = true;
  if (!bp_mq.connected()) {
    if (now - bp_lastMqTry > 10000 || bp_lastMqTry == 0) {
      bp_lastMqTry = now;
      bp_mqConnect();
    }
    return;
  }
  bp_mq.loop();
}

static void bp_netOff() {
  if (bp_mq.connected()) {
    for (int i = 0; i < 5; i++) { bp_mq.loop(); delay(20); }   // let the report go out
    bp_mq.disconnect();
  }
  WiFi.disconnect(true, false);             // radio off, keep the saved network
  WiFi.mode(WIFI_OFF);
}

// =====================================================================
//  Motion: gentle wiggles (direction), hard shake, laid flat, look
// =====================================================================
static void bp_motionInit(const float a[3]) {
  for (int k = 0; k < 3; k++) { bp_g[k] = a[k]; bp_s[k] = a[k]; }
  bp_ws[0] = bp_ws[1] = 0;
  bp_wr[0] = bp_wr[1] = 0;
  bp_we[0] = bp_we[1] = 0;
  bp_hardHits = 0;
  float gm = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  bp_flat = gm > 0.3f && fabsf(a[SCREEN_NORMAL_AXIS]) / gm > 0.80f;
  bp_flatSince = 0;
}

// 0 nothing, 1 up-down wiggle, 2 side-side wiggle, 3 hard shake, 4 just laid flat
static int bp_motion(const float a[3], uint32_t now) {
  float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);

  if (bp_hardHits > 0 && now - bp_hardFirst > 700) bp_hardHits = 0;
  if (mag > BEEP_HARD_G && now - bp_hardLast > 50) {
    if (bp_hardHits == 0) bp_hardFirst = now;
    bp_hardLast = now;
    if (++bp_hardHits >= BEEP_HARD_HITS) {
      bp_hardHits = 0;
      bp_wr[0] = bp_wr[1] = 0;
      bp_wLock = now;
      return 3;
    }
  }

  for (int k = 0; k < 3; k++) {
    bp_g[k] += (a[k] - bp_g[k]) * 0.12f;    // ~0.25 s
    bp_s[k] += (a[k] - bp_s[k]) * 0.01f;    // ~3 s: "how it is resting"
  }

  const int ax[2] = {SCREEN_UP_AXIS, SCREEN_RIGHT_AXIS};
  int fired = -1;
  for (int i = 0; i < 2; i++) {
    float d = a[ax[i]] - bp_g[ax[i]];
    bp_we[i] = bp_we[i] * 0.92f + d * d;
    int8_t s = d > BEEP_WIGGLE_G ? 1 : d < -BEEP_WIGGLE_G ? -1 : 0;
    if (s != 0 && s != bp_ws[i]) {
      if (bp_ws[i] != 0) {
        uint32_t gap = now - bp_wt[i];
        bp_wr[i] = (gap >= 60 && gap <= 450) ? bp_wr[i] + 1 : 1;
        if (bp_wr[i] >= BEEP_WIGGLE_REVS) fired = i;
      }
      bp_wt[i] = now;
      bp_ws[i] = s;
    }
  }
  if (fired >= 0 && now - bp_wLock > 900 && bp_hardHits < 2) {
    if (bp_we[1 - fired] > bp_we[fired]) fired = 1 - fired;   // the stronger direction wins
    bp_wr[0] = bp_wr[1] = 0;
    bp_wLock = now;
    return fired == 0 ? 1 : 2;
  }

  float gm = sqrtf(bp_g[0] * bp_g[0] + bp_g[1] * bp_g[1] + bp_g[2] * bp_g[2]);
  if (gm > 0.3f) {
    float fl = fabsf(bp_g[SCREEN_NORMAL_AXIS]) / gm;
    if (!bp_flat && fl > 0.93f) {
      if (!bp_flatSince) bp_flatSince = now;
      if (now - bp_flatSince > 300) { bp_flat = true; bp_flatSince = 0; return 4; }
    } else if (!bp_flat) {
      bp_flatSince = 0;
    }
    if (bp_flat && fl < 0.70f) bp_flat = false;
  }
  return 0;
}

// eyes slide the way it is tipped, then drift back as it settles
static void bp_tiltLook(float &lx, float &ly) {
  float u, r, u0, r0;
  screenAngles(bp_g, u, r);
  screenAngles(bp_s, u0, r0);
  lx = bp_clamp((long)((r - r0) * 0.6f), -12, 12);
  ly = bp_clamp((long)(-(u - u0) * 0.4f), -8, 8);
}

// =====================================================================
//  Drawing helpers (screen furniture lives in beep_eyes.ino)
// =====================================================================
static void bp_pushEyes() {
  tft.drawRGBBitmap(BEEP_CV_X, BEEP_CV_Y, bp_cv->getBuffer(), BEEP_CV_W, BEEP_CV_H);
}

// =====================================================================
//  The session
// =====================================================================
int runBeep() {
  if (DEBUG_USB) Serial.println("Beep start");
  bp_cv = new GFXcanvas16(BEEP_CV_W, BEEP_CV_H);
  if (!bp_cv || !bp_cv->getBuffer()) {
    if (bp_cv) delete bp_cv;
    bp_cv = NULL;
    tft.fillScreen(C_BLACK);
    drawMultiline("Beep needs\nmore memory", 120, 170, C_DANGER);
    delay(2000);
    return 0;
  }

  bp_defaults();
  uint32_t best = prefs.getUInt(GAMES[0].key, 0);
  snprintf(bp_cid, sizeof(bp_cid), "beep-%08lx", (unsigned long)(ESP.getEfuseMac() & 0xFFFFFFFF));
  bp_mq.setCallback(bp_onMsg);
  bp_mq.setBufferSize(512);
  bp_mq.setKeepAlive(30);
  bp_mq.setSocketTimeout(3);
  bp_linked = bp_gotSettings = bp_stopCmd = bp_skipCmd = false;
  bp_hbVal = '0';
  bp_lastMqTry = 0;
  bp_mqOkAt = 0;
  bp_wifiEverOk = false;

  configureTiltMode();
  float a[3] = {0, 0, 1};
  sampleAccel(a);
  bp_motionInit(a);

  tft.fillScreen(C_BLACK);
  bp_wifiStart();

  // ---- session state ----
  int phase = BP_HELLO;
  uint32_t now = millis();
  uint32_t phaseStart = now, lastTick = now;
  int32_t left = 0, total = 1;
  int round = 1, done = 0, strikes = 0;
  bool angry = false, warn = false, quitEarly = false, gaveUp = false, record = false;
  uint32_t probSince = 0, calmSince = 0, angrySince = 0;
  uint32_t lastStatus = 0;
  uint32_t sessionCap = 0;
  bool statusDirty = true;
  bool reportSent = false;

  // ---- face state ----
  BeepFace f;
  f.color = BEEP_COLORS[bp_cfg.color];
  f.open = 1;
  f.lookX = f.lookY = 0;
  f.anger = 1;
  f.style = BEEP_HAPPY;
  f.t = now;
  uint8_t ambient = BEEP_CHILL, evStyle = 255, doneStyle = BEEP_HEART;
  uint32_t evUntil = 0;
  int vi = 0, hi = 0;
  uint32_t nextRandom = now + 1000UL * (BEEP_RANDOM_MIN_S + esp_random() % (BEEP_RANDOM_MAX_S - BEEP_RANDOM_MIN_S));
  uint32_t nextBlink = now + 2500 + esp_random() % 3500, blinkStart = 0;
  uint32_t nextGlance = now + 3000, glanceUntil = 0;
  float glX = 0, glY = 0, lookXf = 0, lookYf = 0;

  // ---- what is on screen ----
  uint32_t lastRender = 0;
  uint32_t lastKey = 0xFFFFFFFF;
  int shownMode = -1;                     // centre: 0 eyes, 1 clock, 2 note
  long shownSecs = -1;
  uint16_t shownClockCol = 1;
  uint32_t peekUntil = 0;                 // eyes shown over the clock until then

  auto event = [&](uint8_t st, uint32_t ms) { evStyle = st; evUntil = millis() + ms; };

  while (true) {
    now = millis();
    uint32_t dt = now - lastTick;
    lastTick = now;
    f.t = now;

    // ---------------- inputs ----------------
    int gesture = 0;
    if (readAccelG(a)) gesture = bp_motion(a, now);
    bp_netTick(now);
    bool online = bp_mq.connected();

    bool quitAsk = bp_stopCmd;
    bp_stopCmd = false;
    if (gesture == 3) {                                   // hard shake
      bool dizzyNow = (evStyle == BEEP_DIZZY && now < evUntil);
      if (dizzyNow || phase == BP_DONE) quitAsk = true;   // second hard shake = quit
      else if (!angry) event(BEEP_DIZZY, 2200);
    } else if (gesture == 1 && !angry) {                  // up-down wiggle
      ambient = BP_VERT[vi++ % sizeof(BP_VERT)];
      evStyle = 255;
      peekUntil = now + 5000;
      nextRandom = now + 1000UL * BEEP_RANDOM_MAX_S;
    } else if (gesture == 2 && !angry) {                  // side-side wiggle
      ambient = BP_HORIZ[hi++ % sizeof(BP_HORIZ)];
      evStyle = 255;
      peekUntil = now + 5000;
      nextRandom = now + 1000UL * BEEP_RANDOM_MAX_S;
    } else if (gesture == 4 && !angry && phase != BP_DONE) {
      event(BEEP_SQUINT, 2500);                           // laid flat: too bright!
    }
    if (gesture && DEBUG_USB) Serial.printf("Beep gesture %d\n", gesture);

    if (quitAsk && phase != BP_DONE) {
      quitEarly = (phase == BP_FOCUS || phase == BP_BREAK);
      doneStyle = quitEarly ? BEEP_CRY : BEEP_HEART;
      phase = BP_DONE;
      phaseStart = now;
      statusDirty = true;
    } else if (quitAsk && phase == BP_DONE) {
      break;                                              // leave the summary early
    }

    // ---------------- phases ----------------
    if (phase == BP_HELLO) {
      bool settingsWaitOver = bp_gotSettings ||                              // got them
                              (online && now - bp_mqOkAt > BEEP_SETTINGS_MS) ||   // none saved yet
                              (bp_lastMqTry && !online) ||                        // relay unreachable
                              now - phaseStart > BEEP_WIFI_MS;                    // no Wi-Fi
      if (now - phaseStart > 2500 && settingsWaitOver) {
        f.color = BEEP_COLORS[bp_cfg.color];
        phase = BP_FOCUS;                         // no words on the face: straight to the clock
        phaseStart = now;
        left = total = bp_cfg.focusMin * 60000L;
        ambient = BEEP_FOCUSED;
        sessionCap = now + (uint32_t)bp_cfg.rounds * (bp_cfg.focusMin + bp_cfg.breakMin) * 60000UL
                         + 15UL * 60000UL;
        statusDirty = true;
        if (DEBUG_USB) Serial.printf("Beep: %s, %s\n", bp_wifiEverOk ? "Wi-Fi ok" : "offline",
                                     online ? "relay ok" : "no relay");
      }
    } else if (phase == BP_FOCUS) {
      // ---- the phone check ----
      bool checking = bp_cfg.check && bp_linked && online && now - phaseStart > 15000;
      bool problem = checking && (now - bp_lastHb > BEEP_HB_WARN_MS || bp_hbVal != '1');
      if (problem) { if (!probSince) probSince = now; } else probSince = 0;

      if (!angry && problem && now - probSince > BEEP_ANGRY_MS) {
        angry = true;
        angrySince = now;
        strikes++;
        statusDirty = true;
        if (DEBUG_USB) Serial.printf("Beep: ANGRY (strike %d)\n", strikes);
      } else if (angry) {
        if (!problem) {
          if (!calmSince) calmSince = now;
          if (now - calmSince > BEEP_CALM_MS) {
            angry = false;
            calmSince = 0;
            event(BEEP_HAPPY, 1500);                      // relieved
            statusDirty = true;
          }
        } else {
          calmSince = 0;
        }
        if (angry && now - angrySince > BEEP_GIVEUP_MS) { // she left: save the battery
          gaveUp = true;
          doneStyle = BEEP_SLEEPY;
          phase = BP_DONE;
          phaseStart = now;
          statusDirty = true;
        }
      }
      bool newWarn = problem && !angry;
      if (newWarn != warn) { warn = newWarn; statusDirty = true; }
      f.anger = strikes < 1 ? 1 : strikes > 3 ? 3 : strikes;

      if (!angry) left -= dt;                             // the timer pauses while angry
      if (phase == BP_FOCUS && left <= 0) {
        done++;
        record = done > (int)best;
        if (round >= bp_cfg.rounds) {
          doneStyle = record ? BEEP_STAR : BEEP_HEART;
          phase = BP_DONE;
        } else {
          event(record ? BEEP_STAR : BEEP_HEART, 3500);
          phase = BP_BREAK;
          left = total = bp_cfg.breakMin * 60000L;
        }
        phaseStart = now;
        statusDirty = true;
      }
    } else if (phase == BP_BREAK) {
      angry = warn = false;
      probSince = calmSince = 0;
      left -= dt;
      if (bp_skipCmd) { left = 0; bp_skipCmd = false; }
      if (left <= 0) {
        round++;
        phase = BP_FOCUS;
        phaseStart = now;
        left = total = bp_cfg.focusMin * 60000L;
        ambient = BEEP_FOCUSED;
        event(BEEP_SURPRISED, 1500);                      // "oh! back to work"
        statusDirty = true;
      }
    } else if (phase == BP_DONE) {
      angry = warn = false;
      if (!reportSent) {                                  // first tick in DONE: send the report
        reportSent = true;
        char rep[160];
        snprintf(rep, sizeof(rep),
                 "{\"done\":%d,\"of\":%d,\"min\":%u,\"strikes\":%d,\"end\":\"%s\",\"record\":%d}",
                 done, bp_cfg.rounds, bp_cfg.focusMin, strikes,
                 quitEarly ? "quit" : gaveUp ? "gaveup" : "finished", record ? 1 : 0);
        bp_pubTopic("report", rep, true);
      }
      if (now - phaseStart > 6000) break;
    }
    if (sessionCap && now > sessionCap && phase != BP_DONE) {   // safety net
      gaveUp = true;
      doneStyle = BEEP_SLEEPY;
      phase = BP_DONE;
      phaseStart = now;
      statusDirty = true;
    }
    if (bp_skipCmd && phase != BP_BREAK) bp_skipCmd = false;

    // ---------------- status to the page ----------------
    if (online && (statusDirty || now - lastStatus > BEEP_STATUS_MS)) {
      static const char* const PH[5] = {"hello", "-", "focus", "break", "done"};
      char st[200];
      snprintf(st, sizeof(st),
               "{\"ph\":\"%s\",\"left\":%ld,\"tot\":%ld,\"r\":%d,\"of\":%d,\"done\":%d,"
               "\"strikes\":%d,\"angry\":%d,\"warn\":%d,\"mood\":\"%s\",\"link\":%d,\"fw\":\"%s\"}",
               PH[phase], (long)(left > 0 ? left / 1000 : 0), (long)(total / 1000), round,
               bp_cfg.rounds, done, strikes, angry, warn, BP_MOOD[f.style], bp_linked, FW_STR);
      bp_pubTopic("status", st, false);
      lastStatus = now;
    }
    statusDirty = false;

    // ---------------- which eyes ----------------
    if (now > nextRandom) {
      uint8_t pool[sizeof(BP_VERT) + sizeof(BP_HORIZ)];
      memcpy(pool, BP_VERT, sizeof(BP_VERT));
      memcpy(pool + sizeof(BP_VERT), BP_HORIZ, sizeof(BP_HORIZ));
      uint8_t pick;
      do { pick = pool[esp_random() % sizeof(pool)]; } while (pick == ambient);
      ambient = pick;
      peekUntil = now + 5000;                             // Beep peeks out over the clock
      uint32_t lo = BEEP_RANDOM_MIN_S, hi2 = BEEP_RANDOM_MAX_S;
      if (phase == BP_BREAK) { lo = 20; hi2 = 40; }       // more company during breaks
      nextRandom = now + 1000UL * (lo + esp_random() % (hi2 - lo));
    }
    uint8_t style;
    if (phase == BP_DONE)                         style = doneStyle;
    else if (angry)                               style = BEEP_ANGRY;
    else if (warn)                                style = BEEP_SIDEEYE;
    else if (evStyle != 255 && now < evUntil)     style = evStyle;
    else if (phase == BP_HELLO)                   style = BEEP_HAPPY;
    else if (phase == BP_BREAK && left < 60000)   style = BEEP_SLEEPY;
    else                                          style = ambient;
    if (evStyle != 255 && now >= evUntil) evStyle = 255;
    f.style = style;

    // blink
    f.open = 1;
    if (blinkStart) {
      uint32_t bt = now - blinkStart;
      if (bt < 80) f.open = 1.0f - bt / 80.0f;
      else if (bt < 160) f.open = (bt - 80) / 80.0f;
      else { blinkStart = 0; nextBlink = now + 2500 + esp_random() % 3500; }
    } else if (now > nextBlink) {
      blinkStart = now;
    }
    if (be_blinkHalf(style) == 0) f.open = 1;

    // look: tilt + little glances when nothing is happening
    if (now > nextGlance) {
      glX = (int)(esp_random() % 13) - 6;
      glY = (int)(esp_random() % 7) - 3;
      glanceUntil = now + 1200;
      nextGlance = now + 3000 + esp_random() % 4000;
    }
    if (now > glanceUntil) { glX = 0; glY = 0; }
    float tx, ty;
    bp_tiltLook(tx, ty);
    tx += glX;
    ty += glY;
    if (style == BEEP_ANGRY || style == BEEP_SIDEEYE || style == BEEP_DIZZY) { tx = 0; ty = 0; }
    lookXf += (tx - lookXf) * 0.3f;
    lookYf += (ty - lookYf) * 0.3f;
    f.lookX = (int8_t)lroundf(lookXf);
    f.lookY = (int8_t)lroundf(lookYf);

    // ---------------- draw ----------------
    // Centre: the clock during focus / break; eyes for everything else, for
    // reactions (angry, side-eye, dizzy, squint, hearts...), after a wiggle,
    // and when Beep peeks out every so often.
    bool timerPhase = (phase == BP_FOCUS || phase == BP_BREAK);
    bool eyesNow = !timerPhase || angry || warn || (evStyle != 255 && now < evUntil) || now < peekUntil;
    int mode = eyesNow ? 0 : 1;
    uint16_t phaseCol = (phase == BP_BREAK) ? BP_MINT : f.color;

    if (mode == 1) {
      long secs = (left + 999) / 1000;
      if (secs < 0) secs = 0;
      if (shownMode != 1 || secs != shownSecs || phaseCol != shownClockCol) {
        beepDrawClock(*bp_cv, secs, phaseCol);
        bp_pushEyes();
        shownSecs = secs;
        shownClockCol = phaseCol;
      }
    } else {
      bool animated = style == BEEP_BLURRY || style == BEEP_HYPNO || style == BEEP_HEART ||
                      style == BEEP_STAR || style == BEEP_SLEEPY || style == BEEP_DIZZY ||
                      style == BEEP_CRY || (style == BEEP_ANGRY && f.anger >= 2);
      uint32_t key = ((uint32_t)style << 24) ^ ((uint32_t)(f.open * 15) << 20) ^
                     ((uint32_t)(uint8_t)f.lookX << 12) ^ ((uint32_t)(uint8_t)f.lookY << 4) ^
                     f.anger ^ ((uint32_t)f.color << 8);
      if (shownMode != 0 || key != lastKey || (animated && now - lastRender >= BEEP_ANIM_MS)) {
        beepDrawFace(*bp_cv, f);
        bp_pushEyes();
        lastKey = key;
        lastRender = now;
      }
    }
    shownMode = mode;

    uint32_t spent = millis() - now;
    delay(spent < BEEP_LOOP_MS ? BEEP_LOOP_MS - spent : 1);
  }

  bp_netOff();
  delete bp_cv;
  bp_cv = NULL;
  if (DEBUG_USB) Serial.printf("Beep end: %d rounds, %d strikes\n", done, strikes);
  return done;
}
