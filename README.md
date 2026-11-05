# Tiya's Magic 8-Ball

ESP32-C3 SuperMini + LIS3DH + GC9A01 round TFT keychain: shake for an answer,
hold it edge-on for games (tilt runner, roulette, snake, marble maze,
platformer), and update it over Wi-Fi from this repo.

- `Magic8Ball/`: the Arduino sketch (open `Magic8Ball/Magic8Ball.ino`).
  See `Magic8Ball/README.md` for tabs, controls and tuning.
- `firmware/`: what the keychain downloads. **Written by the build workflow, do not edit by hand.**
  `version.txt` is `<version> <md5> <size>`.
- `.github/workflows/build.yml`: compiles every push for the ESP32-C3
  (Minimal SPIFFS partition scheme) and publishes `firmware/` when
  `FW_VERSION` in `Magic8Ball.ino` goes up.

## Shipping an update
1. Change the sketch, bump `FW_VERSION`.
2. Push to `main`, then wait for the green tick on the Actions tab.
3. On the keychain: open the games menu, pick **Update**.

## First-time USB upload
- Board: ESP32C3 Dev Module, USB CDC On Boot: Enabled
- Partition Scheme: **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)**
- Libraries: Adafruit GFX Library, Adafruit GC9A01A, WiFiManager (tzapu)
