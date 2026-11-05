// =====================================================================
//  ota.ino — over-the-air update from GitHub
//
//  Menu -> "Update":
//    1. Wi-Fi: uses the saved network. If there is none (or it fails),
//       the keychain opens its own hotspot (OTA_AP_NAME). Join it from a
//       phone, a page pops up, pick the home Wi-Fi + password. Saved.
//    2. Downloads OTA_BASE_URL/version.txt  ("<version> <md5> <size>")
//    3. Newer than FW_VERSION?  Download firmware.bin with a progress ring,
//       check its MD5, switch to it and restart.
//  If anything fails, the current firmware simply keeps running.
//  Shake at any point to cancel. Wi-Fi is switched fully off afterwards.
//
//  Publishing a new version: bump FW_VERSION in Magic8Ball.ino and push to
//  GitHub. The repo's Actions workflow compiles it and updates /firmware.
// =====================================================================

static void otaScreen(const char* title, const char* l1, const char* l2, uint16_t col) {
  tft.fillScreen(C_BLACK);
  drawMultiline(title, 84, 170, col);
  if (l1) drawCentered(l1, 134, &FreeSans9pt7b, 1, C_WHITE);
  if (l2) drawCentered(l2, 160, &FreeSans9pt7b, 1, C_DIMTEXT);
}

// Pause that a shake can cut short
static void otaHold(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (shakePoll()) return;
    delay(20);
  }
}

static void otaRing(int pct) {
  static int lastDots = -1;
  if (pct < 0) { lastDots = -1; return; }          // reset
  int dots = pct * 36 / 100;
  if (dots == lastDots) return;
  for (int i = (lastDots < 0 ? 0 : lastDots); i < dots; i++) {
    float ang = i * 2.0f * PI / 36 - PI / 2.0f;
    tft.fillCircle(120 + (int)(110 * cosf(ang)), 120 + (int)(110 * sinf(ang)), 3, C_YELLOW);
  }
  lastDots = dots;
  char b[8];
  snprintf(b, sizeof(b), "%d%%", pct);
  tft.fillRect(70, 186, 100, 26, C_BLACK);
  drawCentered(b, 198, &FreeSansBold12pt7b, 1, C_YELLOW);
}

static void otaWifiOff() {
  WiFi.disconnect(true, false);                     // radio off, keep saved network
  WiFi.mode(WIFI_OFF);
}

// ---------------- Wi-Fi (saved network, else setup hotspot) ----------------
static bool otaConnect() {
  otaScreen("Update", "connecting to Wi-Fi", NULL, C_YELLOW);
  WiFi.mode(WIFI_STA);

  WiFiManager wm;
  wm.setDebugOutput(DEBUG_USB);
  wm.setConnectTimeout(OTA_CONNECT_S);
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(OTA_PORTAL_S);
  wm.setTitle("Tiya's Magic 8-Ball");

  if (wm.autoConnect(OTA_AP_NAME)) return true;     // saved network worked

  // Hotspot is up: walk her through it on the screen
  tft.fillScreen(C_BLACK);
  drawMultiline("Wi-Fi setup", 62, 170, C_YELLOW);
  drawCentered("on a phone, join", 106, &FreeSans9pt7b, 1, C_WHITE);
  drawCentered(OTA_AP_NAME, 132, &FreeSansBold12pt7b, 1, C_GAME);
  drawCentered("then pick home Wi-Fi", 160, &FreeSans9pt7b, 1, C_WHITE);
  drawCentered("shake to cancel", 196, &FreeSans9pt7b, 1, C_RINGDIM);
  if (DEBUG_USB) Serial.printf("OTA: setup hotspot %s\n", OTA_AP_NAME);

  shakeReset();
  uint32_t t0 = millis();
  while (millis() - t0 < OTA_PORTAL_S * 1000UL) {
    if (wm.process()) return true;                  // saved + connected
    if (shakePoll()) break;
    delay(10);
  }
  wm.stopConfigPortal();
  return false;
}

// ---------------- version.txt ----------------
static bool otaFetchInfo(int &ver, char* md5, int &size) {
  WiFiClientSecure client;
  client.setInsecure();                             // integrity comes from the MD5
  HTTPClient http;
  http.setTimeout(15000);
  char url[200];
  snprintf(url, sizeof(url), "%sversion.txt?n=%lu", OTA_BASE_URL, (unsigned long)esp_random());
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    if (DEBUG_USB) Serial.printf("OTA: version.txt HTTP %d\n", code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  ver = 0;
  size = 0;
  md5[0] = 0;
  int got = sscanf(body.c_str(), "%d %32s %d", &ver, md5, &size);
  if (DEBUG_USB) Serial.printf("OTA: online v%d (%d bytes), running v%d\n", ver, size, FW_VERSION);
  return got == 3 && strlen(md5) == 32 && size > 0;
}

// ---------------- firmware.bin ----------------
static bool otaInstall(const char* md5, int size) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(20000);
  char url[200];
  snprintf(url, sizeof(url), "%sfirmware.bin?n=%lu", OTA_BASE_URL, (unsigned long)esp_random());
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    if (DEBUG_USB) Serial.printf("OTA: firmware.bin HTTP %d\n", code);
    http.end();
    return false;
  }
  int len = http.getSize();
  if (len > 0 && len != size) {                     // version.txt and .bin out of step
    if (DEBUG_USB) Serial.printf("OTA: size %d, expected %d\n", len, size);
    http.end();
    return false;
  }
  if (!Update.begin(size)) {                        // too big for the app slot?
    if (DEBUG_USB) Serial.printf("OTA: Update.begin failed (%s)\n", Update.errorString());
    http.end();
    return false;
  }
  Update.setMD5(md5);

  WiFiClient* stream = http.getStreamPtr();
  static uint8_t buf[1024];
  int done = 0;
  uint32_t lastData = millis();
  shakeReset();
  while (done < size) {
    size_t avail = stream->available();
    if (avail) {
      size_t want = avail < sizeof(buf) ? avail : sizeof(buf);
      if ((int)want > size - done) want = size - done;
      int n = stream->readBytes(buf, want);
      if (n > 0) {
        if ((int)Update.write(buf, n) != n) break;
        done += n;
        lastData = millis();
        otaRing((int)((int64_t)done * 100 / size));
      }
    } else {
      if (!http.connected()) break;
      if (millis() - lastData > 20000) break;       // stalled
      if (shakePoll()) break;                       // cancelled
      delay(2);
    }
  }
  http.end();

  if (done != size) {
    if (DEBUG_USB) Serial.printf("OTA: got %d of %d bytes\n", done, size);
    Update.abort();
    return false;
  }
  if (!Update.end()) {                              // MD5 mismatch lands here
    if (DEBUG_USB) Serial.printf("OTA: Update.end failed (%s)\n", Update.errorString());
    return false;
  }
  return true;
}

// =====================================================================
void runUpdate() {
  if (DEBUG_USB) Serial.printf("OTA: running v%d\n", FW_VERSION);
  shakeReset();

  if (!otaConnect()) {
    otaWifiOff();
    otaScreen("No Wi-Fi", "try again later", NULL, C_DANGER);
    otaHold(2500);
    return;
  }

  otaScreen("Update", "checking...", NULL, C_YELLOW);
  int ver = 0, size = 0;
  char md5[33];
  char line[32];

  if (!otaFetchInfo(ver, md5, size)) {
    otaScreen("Hmm", "can't reach GitHub", "try again later", C_DANGER);
  } else if (ver <= FW_VERSION) {
    snprintf(line, sizeof(line), "version %d", FW_VERSION);
    otaScreen("Up to date", line, NULL, C_GAME);
  } else {
    snprintf(line, sizeof(line), "v%d -> v%d", FW_VERSION, ver);
    otaScreen("New version!", line, "don't shake me", C_YELLOW);
    otaRing(-1);
    otaRing(0);
    if (otaInstall(md5, size)) {
      otaWifiOff();
      otaScreen("Done!", "restarting...", NULL, C_GAME);
      if (DEBUG_USB) { Serial.println("OTA: done, restarting"); Serial.flush(); }
      delay(1500);
      ESP.restart();                                // never returns
    }
    otaScreen("Update failed", "old version kept", "try again later", C_DANGER);
  }

  otaWifiOff();
  otaHold(2500);
}
