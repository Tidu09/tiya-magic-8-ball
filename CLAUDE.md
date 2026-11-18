# Notes for Claude

- Read `Magic8Ball/README.md` before changing the sketch: tab layout, settled
  axis constants, and the **input rules for every game**.
- Input rules in short: every game frame calls `tiltBoth()` (feeds shake-to-quit),
  flick gestures use the game's OWN detector (like `pfJump`, `coinFlick`), and
  `menuFlick()` is never used inside a game (it resets the shake counter).
- Unsigned time maths: write `-(float)t`, never `-t`, when `t` comes from
  `millis()`. That bug froze Coin flip (v10-v13). Float drawing loops must
  check `isfinite` and clamp to the canvas.
- Versions are semantic: bump `FW_MAJOR` / `FW_MINOR` / `FW_PATCH` in
  `Magic8Ball/Magic8Ball.ino` (PATCH = fix/tuning, MINOR = feature/game,
  MAJOR = big change). Never edit `FW_VERSION` itself: it is derived, and it
  must stay a whole number so older keychains can still update.
- Ship an update: bump the version as above, push to
  `main`, wait for the Build firmware workflow to publish `firmware/`.
