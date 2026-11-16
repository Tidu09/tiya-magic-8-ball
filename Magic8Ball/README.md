# Magic 8-Ball keychain — tab layout

Put all files in one folder named `Magic8Ball`, then open `Magic8Ball.ino`.
The IDE shows the rest as tabs automatically.

| Tab | Holds |
|---|---|
| `Magic8Ball.ino` | every `#define`, struct, global, forward declaration, `setup()` |
| `sensor.ino` | LIS3DH registers, the three sensor modes, shake confirm, tilt, calibration |
| `display.ino` | display power, starfield, text helpers, triangle layout, HUD |
| `fortune.ino` | greeting, galaxy loader, answer screen, 8-ball screen |
| `menu.ino` | game picker, shared game-over screen |
| `game_run.ino` | tilt runner |
| `game_snake.ino` | snake |
| `game_maze.ino` | marble maze |
| `game_platform.ino` | platformer |
| `game_roulette.ino` | European roulette (0-36) |
| `beep_eyes.ino` | Beep's 17 eye styles + timer/ring/header drawing (pure Adafruit_GFX) |
| `beep_run.ino` | Beep session: Pomodoro, phone check over MQTT, gestures |
| `ota.ino` | over-the-air update from GitHub (Wi-Fi setup hotspot, download, restart) |

Rules that keep this working: constants and globals live only in the main tab,
every cross-tab function has a forward declaration there, and structs are
defined before anything uses them.

## Over-the-air updates

The menu's **Update** row (just above Sleep) connects to Wi-Fi and installs a
newer version from GitHub (`Tidu09/tiya-magic-8-ball`, folder `firmware/`).

- First time, or a new home network: the keychain shows "Wi-Fi setup".
  On a phone, join the Wi-Fi called **Tiya-8Ball**; a page opens (or go to
  192.168.4.1); pick the home Wi-Fi and type its password. It is remembered.
- Up to date -> "Up to date". Newer -> progress ring, MD5 check, restart.
  A failed or cancelled download keeps the current version. Shake cancels.
- Shipping an update: bump `FW_VERSION` in `Magic8Ball.ino`, push to `main`.
  The GitHub Actions workflow compiles for the ESP32-C3 and, if the number
  went up, publishes `firmware/firmware.bin` + `firmware/version.txt`.
- One-time USB setup: Partition Scheme "Minimal SPIFFS (1.9MB APP with OTA)",
  library "WiFiManager" by tzapu installed. Every update must keep the
  Update feature working, because it is the only way back in without USB.

## Controls

- **Shake** → 8-ball answer (default).
- **Shake, then hold the clip edge-on, LEFT edge down** during "Hi Tiya!" → calibration → game menu.
- **Shake, then hold it edge-on, RIGHT edge down** → **Beep** (study buddy). Also the first menu row.
- **Menu:** FLICK the clip (one quick jerk, not a tilt) to move to the next entry,
  wrapping round. Hold SIDEWAYS ~0.8 s to start. Shake, or do nothing for 20 s,
  to sleep. Tune with `MENU_FLICK_G` (lower = easier flick).
- **In a game:** dying restarts the same game automatically. Shake at any time
  (during play or on the game-over screen) to go back to the menu.
- **Runner:** tilt up/down to steer through gaps.
- **Snake:** tilt sets direction; edges wrap around, only self-hits kill.
- **Maze:** tilt rolls the marble; reach the centre before the timer ends.
- **Roulette** (2nd in the menu): flick to change mode. NORMAL just spins and shows
  the number (spin again as often as you like). The bets RED, BLACK, ODD, EVEN,
  1-18, 19-36: a win spins again; score = wins in a row. 0 loses every bet.
  Hold sideways to spin. Finish feel: `ROU_FRET_HILL` / `ROU_POCK_DAMP`.
  The menu now shows 4 rows at a time and scrolls (arrows show there is more).

## Axis constants (settled — do not re-guess)

    SCREEN_NORMAL_AXIS 2
    SCREEN_UP_AXIS     0
    SCREEN_UP_SIGN     1
    SCREEN_RIGHT_AXIS  1
    SCREEN_RIGHT_SIGN  -1

Snake and the marble use vertical inverted on purpose: tipping the top of the
screen away moves them DOWN.

## Tuning

| Constant | Effect |
|---|---|
| `SCREEN_UP_AXIS` / `SCREEN_UP_SIGN` | which sensor axis is screen-vertical, and its direction |
| `SCREEN_RIGHT_AXIS` / `SCREEN_RIGHT_SIGN` | same for screen-horizontal (menu, snake, maze) |
| `SCREEN_NORMAL_AXIS` | axis perpendicular to the screen; used by the edge-on game gesture |
| `TILT_FULL_DEG` | tilt for full deflection in the runner |
| `TURN_DEG` | tilt needed to turn in snake |
| `MAZE_ACCEL` / `MAZE_FRICTION` | how heavy or slippery the marble feels |
| `MAX_PLAY_MS` / `IDLE_ABORT_MS` | battery guards on a running game |

High scores are one NVS key per game (`bestRun`, `bestSnake`, `bestMaze`, `bestPlat`, `bestRoul`) in the
`m8b` namespace, written only when a score improves.

## Beep (study buddy)

Pomodoro timer with eyes. Settings and the phone check come from the web page
in `docs/` (GitHub Pages: https://tidu09.github.io/tiya-magic-8-ball/).

- The page and the keychain meet on a public MQTT broker (`broker.emqx.io`,
  backup `broker.hivemq.com`) under `BEEP_TOPIC`. Page -> keychain:
  `settings` (retained JSON), `hb` (1 = face down, 2 = picked up, 0 = left
  the page), `cmd` (stop / skip). Keychain -> page: `status`, `report` (retained).
- Wi-Fi is the network saved by the Update setup. No Wi-Fi = runs offline with
  the last saved settings and no phone check.
- During focus, once the page has linked: no "1" heartbeat for 2.2 s ->
  side-eye; 2.3 s more -> angry, timer paused, strike +1. Strike 2 = red eyes,
  strike 3+ = shaking crash-out (no text: the eyes say it). Angry for 10 min -> session ends (battery).
- Gestures: up-down wiggle = cute eyes, side-side wiggle = trippy eyes, hard
  shake = dizzy (again while dizzy = quit), laid flat = squint.
- Screen: big seven-segment clock in the middle during focus/break, round dots
  underneath; eyes take over for reactions, wiggles and a random peek (45-90 s,
  20-40 s on breaks). No rim ring, no header text.
- Power: Wi-Fi max modem sleep, 8.5 dBm, static eyes redraw only on change,
  animated ones at ~12 fps, radio off at the end.
- Previews: the eye code is pure Adafruit_GFX, so it can be compiled on a PC
  with a stub Arduino.h and rendered to PNG.
