# BatakType: Project Plan

A C++ game that trains touch typing and reaction time. A 75% keyboard is drawn on screen. One key lights up, a timer counts up in milliseconds (3 d.p.) until you press it, then the next key lights. Score is how many keys you hit in 60 seconds.

This plan has two jobs: get the game built, and teach you how to build software that stays reliable as it grows. Every milestone lists what you build, what you learn, and a "done when" check so you know you can move on.

---

## 1. Goals and scope

**Version 1 (the game)**
- Draw a 75% keyboard.
- Light one random key at a time.
- Show a live timer to 3 decimal places for the current key.
- Run a 60-second round and count correct hits.
- Show a results screen: hits, average reaction, best reaction, slowest keys.

**Version 1 is not**
- Online scores, accounts, or multiplayer.
- Different keyboard layouts (QWERTY 75% only).
- Sound, themes, or settings menus.

These go in the stretch list (section 7) so they cannot swallow the core.

**Decisions made now so you do not have to remake them later**

| Decision | Choice | Why |
|---|---|---|
| Language | C++20 | What you want to learn; modern features make code safer |
| Graphics/input | raylib | Small API, you see results fast, good for learning game loops |
| Build system | CMake | The industry default; also what lets you add tests and CI cleanly |
| Testing | doctest or Catch2 | Single-header-style, easy to start |
| Version control | git, from day one | Every milestone is a commit you can return to |

---

## 2. The most important design idea

**Keep game rules separate from raylib.**

```
 ┌──────────────────────────┐        ┌───────────────────────────┐
 │  core/  (pure C++)       │        │  app/  (raylib)           │
 │  - Layout (key data)     │        │  - window + game loop     │
 │  - Round state machine   │ <───── │  - reads keyboard         │
 │  - Target picker         │        │  - draws keyboard + HUD   │
 │  - Stats                 │        │  - real clock + real RNG  │
 │  - Score file (save/load)│        │                           │
 └──────────────────────────┘        └───────────────────────────┘
        tested automatically              tested by playing it
```

Code in `core/` never calls raylib, never reads the real clock, never uses a real random generator directly. Time and randomness are passed *in*. This is what makes the logic testable: a test can say "the key lit at t=1.000, the player pressed it at t=1.231" and check the result is 0.231, with no window and no waiting.

If you only take one habit from this project, take this one.

---

## 3. Folder layout

```
BatakType/
├── CMakeLists.txt
├── PLAN.md
├── README.md
├── .gitignore
├── .clang-format
├── core/
│   ├── layout.hpp / layout.cpp        keyboard data
│   ├── round.hpp / round.cpp          state machine
│   ├── picker.hpp / picker.cpp        chooses next target
│   ├── stats.hpp / stats.cpp          averages, per-key results
│   └── score_file.hpp / .cpp          save and load personal best
├── app/
│   ├── main.cpp                       window + loop
│   ├── renderer.hpp / .cpp            drawing
│   └── input.hpp / .cpp               raylib key codes -> core events
└── tests/
    ├── layout_tests.cpp
    ├── round_tests.cpp
    ├── picker_tests.cpp
    ├── stats_tests.cpp
    └── score_file_tests.cpp
```

---

## 4. Milestones

Work on one milestone at a time. For each one: make a git branch, write the test first where it says so, commit small and often, then merge when "done when" is true.

### Milestone 0: Set up the project

**Build**
- Install a compiler (Apple clang via Xcode command line tools on your Mac), CMake, and git.
- `git init`, add a `.gitignore` (ignore `build/`).
- Write a `CMakeLists.txt` that fetches raylib and builds a window that opens and closes.
- Turn on strict warnings: `-Wall -Wextra -Wpedantic`, and treat warnings as errors from the start.
- Add a `.clang-format` so formatting is never a debate.

**Learn**
- What a build system does, and why you never build by hand in a real project.
- Why warnings are cheap bugs caught for free.

**Done when:** `cmake -B build && cmake --build build` produces a program that shows an empty window, with zero warnings, and the first commit exists.

### Milestone 1: The keyboard as data

**Build**
- A `Key` struct: label, raylib key code, row, x position, width (in key units).
- A function that returns the full 75% layout.
- **Write tests first:**
  - No two keys have the same key code.
  - No two keys overlap.
  - Every row ends at the same right edge (the keyboard is a rectangle).
  - Every letter A-Z is present.

**Learn**
- Data-driven design: describing the keyboard as a table instead of writing 84 drawing calls.
- Your first unit tests, and why a failing test is useful.

**Done when:** the layout tests pass and you can print the layout to the console.

### Milestone 2: Draw the keyboard

**Build**
- `renderer`: draw each key as a rounded rectangle with its label, scaled to fit the window.
- Draw one hard-coded key in a highlight colour.

**Learn**
- The game loop: input, update, draw.
- Separating "what to draw" (data) from "how to draw" (raylib calls).
- Scaling with window size instead of using magic pixel numbers.

**Done when:** a clean keyboard is on screen, it looks right when you resize the window, and one key glows.

### Milestone 3: Game rules without a window

This is the heart of the project. Do it slowly.

**Build** (all in `core/`, no raylib)
- A `Round` state machine with states: `Ready`, `Playing`, `Finished`.
- Inputs are events with timestamps, for example `on_key_pressed(key, now)` and `tick(now)`.
- The `Picker`: chooses the next target, never the same key twice in a row. It takes a random number source as a parameter.
- **Write tests first:**
  - Pressing the right key records a reaction time and scores a hit.
  - Pressing the wrong key does not score. (Decide the penalty rule and test it.)
  - The round ends exactly at 60.000 seconds, and presses after that are ignored.
  - The picker never repeats the previous key.
  - A fixed random seed gives a repeatable sequence.

**Learn**
- State machines: making impossible states impossible.
- Dependency injection: passing the clock and RNG in so tests control them.
- Testing time without waiting for time.

**Done when:** you can simulate a complete 60-second round in a test that runs in milliseconds.

### Milestone 4: Connect the game to the keyboard

**Build**
- `input`: translate raylib key events into the core's events. Read events from raylib's key queue (`GetKeyPressed`) so no press is missed between frames.
- `main`: feed real time (`GetTime`) into the round each frame.
- Light the current target on the on-screen keyboard.
- Increase the frame rate target so timing resolution is fine.

**Learn**
- Gluing a tested core to an untested shell, and why the shell should be thin.
- Frame timing: your timer is only as precise as one frame (about 4 ms at 240 FPS).

**Done when:** you can play a full round with the keyboard and hits are counted correctly.

### Milestone 5: The HUD and the 3 d.p. timer

**Build**
- Big live timer for the current key, formatted as `0.000`.
- Round countdown (60 to 0), hit counter, last reaction time.
- Wrong-key feedback (a red flash).
- A short random pause before each key lights (suggested 0.2 to 0.6 s) so players cannot predict the rhythm. Make this a constant you can change in one place.

**Learn**
- Formatting numbers properly.
- Keeping "feel" settings as named constants instead of numbers buried in code.

**Done when:** it feels like a Batak board: fast, readable, and you can see exactly how long each key took.

### Milestone 6: Results and stats

**Build**
- `Stats` in `core/`: total hits, average, best, worst, and per-key average reaction.
- A results screen showing these, plus your five slowest keys.
- **Write tests first:** empty round (no hits) must not divide by zero; a single hit; known data gives known averages.

**Learn**
- Edge cases: the empty and one-item cases are where most bugs live.
- Turning raw data into something that helps you improve.

**Done when:** the results screen is correct and a zero-hit round does not crash or show `nan`.

### Milestone 7: Saving the personal best

**Build**
- `score_file`: save and load best score and per-key history to a small text or JSON file.
- Handle every way the file can go wrong: missing, empty, corrupted, wrong version, no permission to write.
- Store a version number in the file so you can change the format later.
- Write the file safely: write to a temporary file, then rename it over the real one, so a crash mid-save cannot destroy your scores.
- **Write tests first** for each failure case above.

**Learn**
- Error handling: use `std::optional` or `std::expected`-style results instead of ignoring failures.
- Never trust data from disk.
- Why atomic writes matter.

**Done when:** you can delete, corrupt, or lock the file and the game still starts cleanly with a clear message.

### Milestone 8: Harden and finish

**Build**
- Turn on sanitizers in a debug build: `-fsanitize=address,undefined`. Play several rounds and fix anything reported.
- Run `clang-tidy` and fix what is worthwhile.
- Test odd cases by hand: hold a key down, mash many keys at once, resize mid-round, minimise and return, pause at the results screen.
- Add GitHub Actions (or similar) so tests run on every push.
- Write the README: what it is, how to build, how to play.
- Tag `v1.0`.

**Learn**
- What sanitizers catch that tests and warnings do not.
- Continuous integration: the project proves it works every time, not just on your machine today.

**Done when:** a clean clone builds and passes tests from scratch using only the README instructions.

---

## 5. Reliability checklist (what "reliable" means here)

Apply these throughout, not just at the end.

1. **Tests for every rule in `core/`.** If a rule can break, it has a test.
2. **Warnings are errors.** A clean build is the baseline.
3. **No raw `new`/`delete`.** Use values, `std::vector`, `std::unique_ptr`.
4. **Inject time and randomness.** Never call the clock or RNG deep inside logic.
5. **Handle failure explicitly.** Anything that touches disk can fail. Decide what happens.
6. **Small commits with clear messages.** Each one builds and passes tests.
7. **Make illegal states hard to represent.** Prefer an enum state machine over several boolean flags.
8. **Constants have names.** `kRoundSeconds = 60.0`, not `60` scattered around.
9. **Fix the cause, then add a test.** Every bug you find becomes a test so it cannot return.
10. **The README is part of the code.** If a stranger cannot build it, it is not finished.

---

## 6. Risks and how to handle them

| Risk | What could go wrong | Plan |
|---|---|---|
| Timer precision | Frame-based input adds up to one frame of error | High FPS cap; read the key queue; state the limit honestly in the README |
| Key codes differ by layout | `KEY_Q` may not be where a non-US keyboard has Q | Target US/UK QWERTY for v1; note this limit; test on your own machine |
| Keys the OS captures | Some keys (Cmd, function keys, Esc, media keys) may be intercepted or not reach the app | Decide early which keys are playable; consider excluding F-keys and modifiers from random targets in v1 |
| Scope creep | Features pile up before the core works | Anything not in section 1 goes to the stretch list |
| Over-engineering | Spending weeks on architecture | Stick to the `core/` and `app/` split; add nothing more until it hurts |

---

## 7. Stretch ideas (after v1.0)

- **Practice mode:** weight the picker towards your slowest keys.
- **Letters-only mode** for pure typing practice.
- **Finger map:** colour keys by which finger should press them, and show per-finger stats.
- **Progress over time:** a history graph of average reaction by day.
- **Difficulty modes:** time limit, wrong-key penalties, shrinking light duration.
- **Sound:** a click on hit and a buzz on a miss.
- **Port to the web** (Emscripten compiles raylib to WebAssembly) so you can share it with a link.

---

## 8. Suggested pace

| Milestone | Rough effort |
|---|---|
| 0: Setup | 1 evening |
| 1: Layout + tests | 1 to 2 evenings |
| 2: Draw keyboard | 1 evening |
| 3: Game rules | 2 to 3 evenings (the important one) |
| 4: Connect input | 1 evening |
| 5: HUD + timer | 1 to 2 evenings |
| 6: Stats | 1 evening |
| 7: Save file | 1 to 2 evenings |
| 8: Harden + finish | 2 evenings |

Go at whatever speed suits you. Finishing milestone 3 properly matters more than finishing quickly.

---

## 9. How we work through it

For each milestone: read it, ask about anything unclear, write the code yourself with me explaining and reviewing, run the tests, commit, tick it off below.

- [ ] 0: Set up the project
- [ ] 1: The keyboard as data
- [ ] 2: Draw the keyboard
- [ ] 3: Game rules without a window
- [ ] 4: Connect the game to the keyboard
- [ ] 5: The HUD and the 3 d.p. timer
- [ ] 6: Results and stats
- [ ] 7: Saving the personal best
- [ ] 8: Harden and finish
