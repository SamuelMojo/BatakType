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

## How to play

| Key | Does |
|---|---|
| `SPACE` / `ENTER` | Start a round (menu or results screen) |
| `A`-`Z` | Hit the letter on screen |
| `ESC` | Close settings, or quit the round back to the menu |
| Gear (top right) | Settings. Pauses the round while it's open |

- **Wrong key:** the timer shakes, turns red and keeps running until you hit the right key. The list on the left shows how many wrong keys each letter took.
- **Random positions:** after a set number of keys (10 on Medium), letters stop appearing in the middle and show up in random places.
- **Focus on missed letters:** letters you get wrong on the first try come up more often. This carries across rounds until you close the game or press **Reset focus** in settings. Get a letter right first time and it comes up less again.

## Settings (gear icon)

### Difficulty presets

The Easy / Medium / Hard buttons set everything except round length. The button for the current preset is filled in. If you move a slider yourself, none is filled in. The game starts on Medium.

| | Random positions after | Letter frequency | Focus on missed letters | Flash letter |
|---|---|---|---|---|
| Easy | Never | 5 / 10 | 40% | off |
| Medium | 10 keys | 7 / 10 | 60% | off |
| Hard | 0 keys (from the start) | 10 / 10 | 80% | on |

### Sliders

| Setting | What it does |
|---|---|
| Round length | How long a round lasts (10-180 s, default 60) |
| Random positions after | When letters start appearing in random places. 0 = from the start, far right = Never |
| Letter frequency | How quickly the next letter comes after you hit one. 10 = straight away, 1 = about 1.35 s. Each step is 0.15 s, with a little randomness so you can't settle into a rhythm |
| Focus on missed letters | How hard the game leans on letters you get wrong (0 = pure random) |
| Flash letter | When ticked, the letter shows for a quarter of a second and then disappears. The timer stays until you press the key |

## Code layout

```
game/
  core.h / core.c        game rules: letter weighting, picking, round stats (no raylib)
  main.c                 window, input, drawing, settings panel
  tests/core_tests.c     tests for core.c
```

`core.c` never touches raylib, the clock or a global random generator. Time and randomness are passed in, so the rules can be tested without a window (see `Docs/PLAN.md`, section 2).

## Notes

- Arial is loaded from the system (`/System/Library/Fonts/Supplemental/Arial.ttf` on macOS, `C:/Windows/Fonts/arial.ttf` on Windows). If it isn't found, raylib's built-in font is used instead.
- Timing is sampled once per frame. The game targets 240 FPS, so times are accurate to about 4 ms.
- Keys are read by physical position (US/UK QWERTY).
