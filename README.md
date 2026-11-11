# BatakType

A Batak-board style reaction trainer for your keyboard, written in C with [raylib](https://www.raylib.com/). Runs as a desktop app or in any modern browser. The web version is a single 380 KB HTML file (about 145 KB to download, compressed).

A letter appears and a timer starts. Hit that key as fast as you can. A round lasts 60 seconds, every reaction time is listed down the side, and at the end you get your total time. Letters you keep getting wrong come up more often, and on harder settings they jump around the screen and vanish almost as soon as they appear.

![Menu](Docs/screenshots/menu.png)

| A letter and its timer | Flash mode: the letter's gone, the timer keeps going |
|---|---|
| ![Letter](Docs/screenshots/letter.png) | ![Flash mode](Docs/screenshots/flash-mode.png) |
| **Wrong key: the timer turns red and shakes** | **Settings, on the Hard preset** |
| ![Wrong key](Docs/screenshots/wrong-key.png) | ![Settings](Docs/screenshots/settings-hard.png) |

## Build and run

You need CMake 3.24+, a C compiler and git. raylib 5.5 is downloaded automatically the first time you configure.

```sh
cmake -B build
cmake --build build
./build/batak_type
```

Run the tests for the game rules (no window needed):

```sh
ctest --test-dir build
```

### In a browser

The same C code compiles to WebAssembly with [Emscripten](https://emscripten.org/). On macOS:

```sh
brew install emscripten
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-web
open build-web/batak_type.html
```

That gives you one self-contained `batak_type.html`, with the code and font inside it. It renders at your screen's full resolution, so text stays sharp on Retina displays. Double-click it, email it, or put it on any web host; no server setup needed. Add `-DBATAK_WEB_SINGLE_FILE=OFF` to the first `cmake` line to get separate `.html` + `.js` + `.wasm` files instead (a little smaller, but they must be served by a web server rather than opened from disk).

**GitHub Pages:** [`.github/workflows/web.yml`](.github/workflows/web.yml) builds the web version on every push to `master` and publishes it. Turn it on once in the repo's *Settings → Pages → Source: GitHub Actions*, and the game is playable at <https://samuelmojo.github.io/BatakType/>.

## How to play

| Key | Does |
|---|---|
| `Space` / `Enter` | Start a round |
| `A`-`Z` | Hit the letter on screen |
| `Esc` | Close settings, or leave the round |
| Gear icon | Settings (pauses the round) |

### Difficulty presets

The Easy, Medium and Hard buttons in settings change everything except round length. You can still move any slider yourself afterwards.

| | Random positions after | Letter frequency | Focus on missed letters | Flash letter |
|---|---|---|---|---|
| Easy | Never | 2 / 10 | 40% | off |
| Medium (default) | 10 keys | 5 / 10 | 60% | off |
| Hard | 0 keys | 8 / 10 | 80% | on |

Hard isn't the ceiling: push *Letter frequency* to 9 or 10 with *Flash letter* on, and the letter is shown for less time. At 10 it's a single screen refresh (see [the maths](#3-letter-frequency-gap-and-flash-time)).

---

## How the maths works

All of the game rules live in [`game/core.c`](game/core.c), which never touches raylib, the clock or a random number generator. Time and randomness are passed in, so every rule can be tested without opening a window ([`game/tests/core_tests.c`](game/tests/core_tests.c)).

### 1. Reaction time

When a letter appears, the game notes the time $t_{\text{shown}}$. When you press the right key at $t_{\text{press}}$, your reaction time is:

$$t = t_{\text{press}} - t_{\text{shown}}$$

- **The clock pauses with the settings menu.** The game clock is `real time − total time spent in settings`, so opening settings mid-round doesn't add to your time.
- **Wrong keys don't stop the clock.** The timer keeps running until you hit the right key, so a wrong key costs you the time it takes to notice and correct it.
- **Precision on desktop.** Keys are read once per frame and the game runs at 240 frames per second, so any time can be up to $1/240 \approx 4.2$ ms late.
- **Precision in a browser.** Browsers only redraw at the screen's refresh rate (usually 60 Hz), so reading keys once per frame would round every time to the nearest $1/60 \approx 16.7$ ms. Instead, the web build listens for key presses directly and notes the exact moment each one happened (`emscripten_get_now()`, accurate to about 0.1 ms in Chrome and 1 ms in Firefox and Safari). That timestamp is converted onto the game clock, so $t_{\text{press}}$ is when your finger hit the key, not when the next frame happened to run.
- Only the first press of a key counts, so holding a key down can't rack up wrong presses.

### 2. Picking the next letter (focus on missed letters)

Every letter keeps a running record across rounds:

- $a$ = how many times you've completed it
- $m$ = how many of those times you pressed a wrong key first (a "first-try miss")

The letter's **weight** is:

$$w = 1 + s \cdot 8 \cdot \frac{m}{a + 2}$$

where $s$ is the *Focus on missed letters* slider as a fraction (60% → $0.6$), and 8 is `ADAPT_GAIN` in `core.h`.

- A letter you've never seen, or never missed, has weight $1$.
- The $+2$ on the bottom smooths things out. One slip on a letter you've only seen once doesn't send it through the roof. It takes repeated misses to push the weight up.
- Each time you get the letter right first time, $a$ goes up and $m$ doesn't, so the weight drifts back towards 1. A letter you've fixed stops being drilled.
- At 100% focus, a letter you miss every single time approaches a weight of $1 + 8 = 9$.

**Choosing.** The game never shows the same letter twice in a row, so the previous letter's weight is set to 0. The chance of letter $i$ coming up next is its share of the total:

$$P(i) = \frac{w_i}{\sum_j w_j}$$

In code this is a "roulette wheel": lay all 26 weights end to end, pick a random point $r \cdot \sum w$ with $r \in [0, 1)$, and take whichever letter that point lands on.

**Worked example.** Say you've done Q 4 times and fumbled it 3 of those times ($a = 4$, $m = 3$), and every other letter is clean.

| Preset (focus) | Q's weight | Chance Q is next | vs. pure random (1 in 25) |
|---|---|---|---|
| Easy (40%) | $1 + 0.4 \cdot 8 \cdot \frac{3}{6} = 2.6$ | 9.8% | ×2.4 |
| Medium (60%) | $1 + 0.6 \cdot 8 \cdot \frac{3}{6} = 3.4$ | 12.4% | ×3.1 |
| Hard (80%) | $1 + 0.8 \cdot 8 \cdot \frac{3}{6} = 4.2$ | 14.9% | ×3.7 |

(The total is 24 other eligible letters at weight 1, plus Q.) If you then get Q right first time for the next 6 goes on Hard, it drops to $1 + 0.8 \cdot 8 \cdot \frac{3}{12} = 2.6$.

*Reset focus* in settings clears every letter's record back to $a = m = 0$.

### 3. Letter frequency: gap and flash time

The *Letter frequency* slider, $f$ from 1 to 10, sets two things. Level 8 is the reference point.

**Gap between letters.** After you hit a letter, the next one waits for:

$$\text{gap} = \max(0,\ 8 - f) \times 0.15\,\text{s} \times u, \qquad u \sim \text{Uniform}(0.7,\ 1.3)$$

So from 8 upwards the next letter comes immediately, and each step below 8 adds 0.15 s. The random factor $u$ changes the gap by up to ±30% so you can't settle into a rhythm and press on the beat.

**Flash time** (only when *Flash letter* is ticked). How long the letter stays on screen before only the timer is left:

$$\text{flash} = \min\!\left(1\,\text{s},\ 0.125\,\text{s} \times 2^{\,8 - f}\right) \quad (f \le 9), \qquad \text{flash} = \text{one screen refresh} \quad (f = 10)$$

Each step up halves it, and each step down doubles it, up to a cap of 1 s. At 10 the letter is drawn for exactly one frame, the shortest time a screen can show anything: 16.7 ms on a 60 Hz display, 8.3 ms at 120 Hz. Whatever the setting, every letter is drawn on at least one frame, so it can never be skipped entirely.

| Frequency | Average gap | Gap range | Flash time |
|---|---|---|---|
| 10 | 0 s | next letter immediately | one refresh (≈ 17 ms at 60 Hz) |
| 9 | 0 s | next letter immediately | 0.0625 s |
| 8 (Hard) | 0 s | next letter immediately | 0.125 s |
| 7 | 0.15 s | 0.11 – 0.20 s | 0.25 s |
| 6 | 0.30 s | 0.21 – 0.39 s | 0.5 s |
| 5 (Medium) | 0.45 s | 0.32 – 0.59 s | 1 s |
| 2 (Easy) | 0.90 s | 0.63 – 1.17 s | 1 s |
| 1 | 1.05 s | 0.74 – 1.37 s | 1 s |

### 4. Random positions

Once you've done the set number of keys (*Random positions after*), each letter is placed at a random point in the play area, to the right of the times list:

- $x$ and $y$ are picked uniformly, with margins so the 160 px letter never touches an edge and there's always room for the timer underneath it.
- The game tries up to 16 random points and takes the first one more than 240 px (1.5 letter heights) from the previous letter. If none are that far, it uses the furthest one it found. So letters always jump somewhere noticeably new, but you can't predict where.

### 5. Flash letter and the wrong-key shake

- **Flash letter:** the letter is drawn only while $t$ is under the flash time from section 3 (0.125 s on Hard). After that just the timer is left, so you have to read the letter in that moment.
- **Shake:** a wrong key starts a 0.35 s shake. With $k$ = the fraction of that time left (1 down to 0), the timer moves by

$$\Delta x = 14\,k^2 \sin(95\,t), \qquad \Delta y = 0.35 \cdot 14\,k^2 \cos(71\,t)$$

The $k^2$ makes it start hard and settle quickly. Using two different frequencies (95 and 71) makes it look like a rattle rather than a smooth circle.

### 6. End-of-round stats

For the $n$ letters you completed in a round, with reaction times $t_1 \dots t_n$:

| Stat | Formula |
|---|---|
| Total time | $\sum t_i$ |
| Average | $\frac{1}{n}\sum t_i$ (a round with no hits is handled separately, so it never divides by zero) |
| Best / Slowest | $\min t_i$ / $\max t_i$, with the letter |
| First try | $\frac{\text{letters with no wrong presses}}{n} \times 100\%$ |
| Missed most | letters with first-try misses this round, sorted by how many times |

A letter that's still on screen when the round ends isn't counted.

---

## Project layout

```
game/                  the game (C11 + raylib)
  core.h / core.c      rules and maths above: weights, picking, stats (no raylib)
  main.c               window, input, drawing, settings, browser support
  font_data.h          embedded 8.8 KB font (see below)
  fonts/OFL.txt        that font's licence
  web/shell.html       the minimal page the browser version runs in
  tests/core_tests.c   tests for core.c
app/                   C++ learning version (see Docs/PLAN.md)
Docs/                  plan, notes and screenshots
.github/workflows/     builds the browser version and publishes it to GitHub Pages
CMakeLists.txt         builds everything, fetches raylib
```

**Font.** On desktop the game uses Arial from your system (`/System/Library/Fonts/Supplemental/Arial.ttf` on macOS, `C:/Windows/Fonts/arial.ttf` on Windows). A browser can't read your system fonts, so the game also carries *BatakSans*: Liberation Sans (same letter shapes and widths as Arial) cut down to the 95 printable ASCII characters, 8.8 KB instead of 410 KB. It's renamed as its licence (SIL Open Font License) requires for modified versions. The desktop app uses it too if Arial isn't installed.
