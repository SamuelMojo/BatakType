# BatakType (C version)

A Batak-board reaction trainer for your keyboard, written in C11 with raylib 5.5. Plain black screen, white Arial text.

A letter appears and a timer starts. Hit that key. Each round lasts 60 seconds, every time you set is listed on the left, and at the end you get your **total time** (all your reaction times added up) plus a breakdown.

## Build and run

From the project root (same CMake setup as the C++ project):

```sh
cmake -B build
cmake --build build
./build/batak_type
```

Run the rule tests (no window needed):

```sh
ctest --test-dir build
```

### In a browser

```sh
brew install emscripten
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-web
open build-web/batak_type.html
```

This makes a single self-contained `batak_type.html` (about 380 KB) that runs in any modern browser, straight from disk. See the main README for details.

## Modes

- **Casual:** 60-second round using your settings. Ends early after 3 wrong presses on one letter, or if a letter isn't pressed within 1 second (Easy, Medium) or half a second (Hard). Headline: average reaction time.
- **Competitive:** starts at frequency 5 and keeps speeding up (after 5, 12, 22, 36, 55... s) to your monitor's top level. Letters always flash, focus on missed letters is off, and one wrong key or half a second on a letter ends the run. Headline: how many letters you got right.

Both show throughput in bits per second (see [Docs/MATHS.md](../Docs/MATHS.md)).

## How to play

Click **Casual** or **Competitive** on the menu to start, and **Play again** on the results screen to go again.

| Key | Does |
|---|---|
| `A`-`Z` | Hit the letter on screen |
| `ESC` | Close settings, or quit the round back to the menu |
| Gear (top right) | Settings. Pauses the round while it's open |

- **Wrong key:** the timer shakes, turns red and keeps running until you hit the right key. The list on the left shows how many wrong keys each letter took.
- **Random positions:** after a set number of keys (10 on Medium), letters stop appearing in the middle and show up in random places.
- **Focus on missed letters:** letters you get wrong on the first try come up more often. This carries across rounds until you close the game or press **Reset focus** in settings. Get a letter right first time and it comes up less again.

## Settings (gear icon)

### Difficulty presets

The Easy / Medium / Hard buttons set everything except round length. The button for the current preset is filled in. If you move a slider yourself, none is filled in. The game starts on Easy.

| | Random positions after | Letter frequency | Focus on missed letters | Flash letter |
|---|---|---|---|---|
| Easy | 10 keys | 5 | 60% | off |
| Medium | 5 keys | 6 | 70% | on |
| Hard | 0 keys (from the start) | 8 | 80% | on |

### Sliders

| Setting | What it does |
|---|---|
| Round length | How long a round lasts (10-180 s, default 60) |
| Random positions after | When letters start appearing in random places. 0 = from the start, far right = Never |
| Letter frequency | How quickly the next letter comes, and how briefly it flashes. From 8 up, the next letter comes straight away; each step below 8 adds about 0.15 s. With Flash letter on, 8 shows the letter for 0.12 s and each step up shows it for 25% less, until it is a single refresh of your monitor (level 15 at 60 Hz, 18 at 120 Hz) |
| Focus on missed letters | How hard the game leans on letters you get wrong (0 = pure random) |
| Flash letter | When ticked, the letter disappears after a moment (how long depends on Letter frequency). The timer stays until you press the key |

## Code layout

```
game/
  core.h / core.c        game rules: letter weighting, picking, round stats (no raylib)
  ui.h / ui.c            sharp text, buttons, mouse, monitor refresh rate
  main.c                 the game: settings, rounds, screens, input
  font_data.h            embedded font for browsers / machines without Arial
  fonts/OFL.txt          that font's licence
  web/shell.html         the page the browser version runs in
  tests/core_tests.c     tests for core.c
```

`core.c` never touches raylib, the clock or a global random generator. Time and randomness are passed in, so the rules can be tested without a window (see `Docs/PLAN.md`, section 2).

## Notes

- Arial is loaded from the system (`/System/Library/Fonts/Supplemental/Arial.ttf` on macOS, `C:/Windows/Fonts/arial.ttf` on Windows). If it isn't found, and always in a browser, the embedded BatakSans font is used instead (Liberation Sans cut down to ASCII, same letter shapes and widths as Arial).
- On desktop, keys are read once per frame at 240 FPS, so times are accurate to about 4 ms. In a browser, each key press is timestamped the moment it happens, so times are accurate to about 1 ms even though the screen only redraws at 60 Hz.
- Keys are read by physical position (US/UK QWERTY).
