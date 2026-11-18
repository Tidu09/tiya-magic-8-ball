# Notes for Claude

- Read `Magic8Ball/README.md` before changing the sketch: tab layout, settled
  axis constants, and the **input rules for every game**.
- Input rules in short: every game frame calls `tiltBoth()` (feeds shake-to-quit),
  flick gestures use the game's OWN detector (like `pfJump`, `coinFlick`), and
  `menuFlick()` is never used inside a game (it resets the shake counter).
- Ship an update: bump `FW_VERSION` in `Magic8Ball/Magic8Ball.ino`, push to
  `main`, wait for the Build firmware workflow to publish `firmware/`.
