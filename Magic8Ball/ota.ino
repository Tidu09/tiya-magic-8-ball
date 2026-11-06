// =====================================================================
//  ota.ino — over-the-air update from GitHub
//
//  Menu -> "Update":
//    1. Wi-Fi: uses the saved network. If there is none (or it fails),
//       the keychain opens its own hotspot (OTA_AP_NAME). Join it from a
//       phone, a page pops up, pick the home Wi-Fi + password. Saved.
//    2. Downloads version.txt  ("<version> <md5> <size> [<file>]") from
//       GitHub, or from the jsDelivr mirror if GitHub can't be reached.
//    3. Newer than FW_VERSION?  Download the firmware with a progress ring,
//       check its MD5, switch to it and restart.
//  If anything fails, the screen says why (login-page Wi-Fi, no internet,
//  blocked server...) and offers: FLICK = change Wi-Fi, SHAKE = leave.
//  The current firmware always keeps running after a failed attempt.
//  Wi-Fi is switched fully off afterwards.
//
//  Publishing a new version: bump FW_VERSION in Magic8Ball.ino and push to
//  GitHub. The repo's Actions workflow compiles it and updates /firmware.
// =====================================================================

static char otaErr[48] = "";                        // last failure, shown small

static void otaScreen(const char* title, const char* l1, const char* l2, uint16_t col) {
  tft.fillScreen(C_BLACK);
  drawMultiline(title, 84, 170, col);
  if (l1) drawCentered(l1, 134, &FreeSans9pt7b, 1, C_WHITE);
  if (l2) drawCentered(l2, 160, &FreeSans9pt7b, 1, C_DIMTEXT);
}

// Tiny built-in-font line, for error details
static void otaTiny(const char* s, int cy, uint16_t col) {
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(col);
  tft.setCursor(120 - (int)(strlen(s) * 6 - 1) / 2, cy - 4);
  tft.print(s);
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

// Re-apply the low TX power every time the radio (re)starts, in station
// or hotspot mode: WiFiManager switches modes on its own.
static void otaTxPowerEvent(WiFiEvent_t event) {
  if (event == ARDUINO_EVENT_WIFI_STA_START || event == ARDUINO_EVENT_WIFI_AP_START) {
    WiFi.setTxPower(OTA_TX_POWER);
  }
}

// ---------------- Wi-Fi ----------------
// forceSetup = true skips the saved network and opens the setup hotspot
// straight away (used for "change Wi-Fi").
static bool otaConnect(bool forceSetup) {
  otaScreen("Update", forceSetup ? "starting Wi-Fi setup" : "connecting to Wi-Fi",
            NULL, C_YELLOW);
  static bool eventHooked = false;
  if (!eventHooked) { WiFi.onEvent(otaTxPowerEvent); eventHooked = true; }
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(OTA_TX_POWER);

  WiFiManager wm;
  wm.setDebugOutput(DEBUG_USB);
  wm.setConnectTimeout(OTA_CONNECT_S);
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(OTA_PORTAL_S);
  wm.setTitle("Tiya's Magic 8-Ball");

  if (forceSetup) {
    if (DEBUG_USB) Serial.println("OTA: change Wi-Fi requested, opening setup hotspot");
    wm.startConfigPortal(OTA_AP_NAME);
  } else {
    if (DEBUG_USB) Serial.println("OTA: Wi-Fi on (low TX power), trying saved network");
    if (wm.autoConnect(OTA_AP_NAME)) {               // saved network worked
      if (DEBUG_USB) Serial.printf("OTA: connected to %s, signal %d dBm\n",
                                   WiFi.SSID().c_str(), WiFi.RSSI());
      return true;
    }
  }

  // Hotspot is up: walk her through it on the screen
  WiFi.setTxPower(OTA_TX_POWER);
  tft.fillScreen(C_BLACK);
  drawMultiline("Wi-Fi setup", 62, 170, C_YELLOW);
  drawCentered("on a phone, join", 106, &FreeSans9pt7b, 1, C_WHITE);
  drawCentered(OTA_AP_NAME, 132, &FreeSansBold12pt7b, 1, C_GAME);
  drawCentered("then pick home Wi-Fi", 160, &FreeSans9pt7b, 1, C_WHITE);
  drawCentered("shake to cancel", 196, &FreeSans9pt7b, 1, C_RINGDIM);
  if (DEBUG_USB) Serial.printf("OTA: setup hotspot %s is up\n", OTA_AP_NAME);

  shakeReset();
  uint32_t t0 = millis();
  while (millis() - t0 < OTA_PORTAL_S * 1000UL) {
    if (wm.process()) {                             // saved + connected
      if (DEBUG_USB) Serial.printf("OTA: connected to %s, signal %d dBm\n",
                                   WiFi.SSID().c_str(), WiFi.RSSI());
      return true;
    }
    if (shakePoll()) break;
    delay(10);
  }
  wm.stopConfigPortal();
  return false;
}

// Is this real internet, a login-page network, or nothing at all?
// 0 = internet, 1 = login page / filtered, 2 = no answer
static int otaInternetCheck() {
  WiFiClient c;
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(c, OTA_PROBE_URL)) return 2;
  int code = http.GET();
  http.end();
  if (DEBUG_USB) Serial.printf("OTA: internet probe -> %d\n", code);
  if (code == 204) return 0;
  return (code > 0) ? 1 : 2;
}

// ---------------- version.txt ----------------
static bool otaFetchInfo(const char* base, int &ver, char* md5, int &size, char* file) {
  char host[40];
  const char* h = strstr(base, "://");
  h = h ? h + 3 : base;
  int hl = 0;
  while (h[hl] && h[hl] != '/' && hl < (int)sizeof(host) - 1) { host[hl] = h[hl]; hl++; }
  host[hl] = 0;

  IPAddress ip;
  if (!WiFi.hostByName(host, ip)) {                 // DNS: blocked or no internet
    snprintf(otaErr, sizeof(otaErr), "DNS failed: %s", host);
    if (DEBUG_USB) Serial.printf("OTA: %s\n", otaErr);
    return false;
  }

  for (int attempt = 0; attempt < 2; attempt++) {
    WiFiClientSecure client;
    client.setInsecure();                           // integrity comes from the MD5
    HTTPClient http;
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    char url[220];
    snprintf(url, sizeof(url), "%sversion.txt?n=%lu", base, (unsigned long)esp_random());
    if (!http.begin(client, url)) { snprintf(otaErr, sizeof(otaErr), "bad URL"); return false; }
    int code = http.GET();
    if (code == 200) {
      String body = http.getString();
      http.end();
      ver = 0;
      size = 0;
      md5[0] = 0;
      strcpy(file, "firmware.bin");
      int got = sscanf(body.c_str(), "%d %32s %d %31s", &ver, md5, &size, file);
      if (DEBUG_USB) Serial.printf("OTA: %s says v%d %s (%d bytes), running v%d\n",
                                   host, ver, file, size, FW_VERSION);
      if (got >= 3 && strlen(md5) == 32 && size > 0) return true;
      snprintf(otaErr, sizeof(otaErr), "bad version.txt");
      return false;
    }
    snprintf(otaErr, sizeof(otaErr), "%s: %s", host,
             code < 0 ? http.errorToString(code).c_str() : String(code).c_str());
    if (DEBUG_USB) Serial.printf("OTA: version.txt try %d -> %d (%s)\n", attempt + 1, code, otaErr);
    http.end();
    delay(500);
  }
  return false;
}

// ---------------- firmware ----------------
static bool otaInstall(const char* base, const char* file, const char* md5, int size) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  char url[220];
  snprintf(url, sizeof(url), "%s%s?n=%lu", base, file, (unsigned long)esp_random());
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    snprintf(otaErr, sizeof(otaErr), "download: %s",
             code < 0 ? http.errorToString(code).c_str() : String(code).c_str());
    if (DEBUG_USB) Serial.printf("OTA: %s -> %d\n", url, code);
    http.end();
    return false;
  }
  int len = http.getSize();
  if (len > 0 && len != size) {                     // version.txt and .bin out of step
    snprintf(otaErr, sizeof(otaErr), "size %d, expected %d", len, size);
    if (DEBUG_USB) Serial.printf("OTA: %s\n", otaErr);
    http.end();
    return false;
  }
  if (!Update.begin(size)) {                        // too big for the app slot?
    snprintf(otaErr, sizeof(otaErr), "no room: %s", Update.errorString());
    if (DEBUG_USB) Serial.printf("OTA: %s\n", otaErr);
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
    snprintf(otaErr, sizeof(otaErr), "got %d of %d bytes", done, size);
    if (DEBUG_USB) Serial.printf("OTA: %s\n", otaErr);
    Update.abort();
    return false;
  }
  if (!Update.end()) {                              // MD5 mismatch lands here
    snprintf(otaErr, sizeof(otaErr), "check failed: %s", Update.errorString());
    if (DEBUG_USB) Serial.printf("OTA: %s\n", otaErr);
    return false;
  }
  return true;
}

// After a failure: explain, then FLICK = change Wi-Fi, SHAKE / wait = leave
static bool otaOfferWifiChange(const char* title, const char* why) {
  tft.fillScreen(C_BLACK);
  drawMultiline(title, 70, 170, C_DANGER);
  if (why) drawCentered(why, 112, &FreeSans9pt7b, 1, C_WHITE);
  if (otaErr[0]) otaTiny(otaErr, 136, C_RINGDIM);
  drawCentered("flick = change Wi-Fi", 166, &FreeSans9pt7b, 1, C_YELLOW);
  drawCentered("shake = exit", 192, &FreeSans9pt7b, 1, C_DIMTEXT);
  if (DEBUG_USB) Serial.printf("OTA: %s / %s / %s\n", title, why ? why : "", otaErr);

  shakeReset();
  uint32_t t0 = millis();
  while (millis() - t0 < OTA_ASK_MS) {
    if (menuFlick()) return true;                   // also feeds the shake detector
    if (shakeSeen()) return false;
    delay(FRAME_MS);
  }
  return false;
}

// =====================================================================
void runUpdate() {
  if (DEBUG_USB) Serial.printf("OTA: running v%d\n", FW_VERSION);
  setCpuFrequencyMhz(160);                          // faster HTTPS, steadier Wi-Fi
  shakeReset();
  bool forceSetup = false;

  for (int round = 0; round < 3; round++) {
    otaErr[0] = 0;

    if (!otaConnect(forceSetup)) {
      otaWifiOff();
      if (otaOfferWifiChange("No Wi-Fi", "couldn't connect")) { forceSetup = true; continue; }
      break;
    }

    otaScreen("Update", "checking...", NULL, C_YELLOW);
    const char* bases[2] = {OTA_BASE_URL, OTA_MIRROR_URL};
    int ver = 0, size = 0, which = -1;
    char md5[33], file[32];
    for (int b = 0; b < 2 && which < 0; b++) {
      if (otaFetchInfo(bases[b], ver, md5, size, file)) which = b;
    }

    if (which < 0) {
      int net = otaInternetCheck();
      const char* why = (net == 1) ? "Wi-Fi wants a login page"
                      : (net == 2) ? "this Wi-Fi has no internet"
                      : "server blocked here";
      if (!otaErr[0]) snprintf(otaErr, sizeof(otaErr), "signal %d dBm", WiFi.RSSI());
      otaWifiOff();
      if (otaOfferWifiChange("Can't reach GitHub", why)) { forceSetup = true; continue; }
      break;
    }

    char line[32];
    if (ver <= FW_VERSION) {
      otaWifiOff();
      snprintf(line, sizeof(line), "version %d", FW_VERSION);
      otaScreen("Up to date", line, NULL, C_GAME);
      otaHold(2500);
      break;
    }

    snprintf(line, sizeof(line), "v%d -> v%d", FW_VERSION, ver);
    otaScreen("New version!", line, "don't shake me", C_YELLOW);
    otaRing(-1);
    otaRing(0);
    bool ok = otaInstall(bases[which], file, md5, size);
    if (!ok && which == 0) {                        // GitHub failed mid-way: try the mirror
      otaRing(-1);
      otaScreen("New version!", line, "trying backup server", C_YELLOW);
      otaRing(0);
      ok = otaInstall(bases[1], file, md5, size);
    }
    if (ok) {
      otaWifiOff();
      otaScreen("Done!", "restarting...", NULL, C_GAME);
      if (DEBUG_USB) { Serial.println("OTA: done, restarting"); Serial.flush(); }
      delay(1500);
      ESP.restart();                                // never returns
    }
    otaWifiOff();
    otaScreen("Update failed", "old version kept", "try again later", C_DANGER);
    if (otaErr[0]) otaTiny(otaErr, 196, C_RINGDIM);
    otaHold(4000);
    break;
  }

  otaWifiOff();
  setCpuFrequencyMhz(80);
}
