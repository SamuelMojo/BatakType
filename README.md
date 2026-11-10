# BatakType

A Batak-board style reaction trainer for your keyboard, written in C with [raylib](https://www.raylib.com/).

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
| Easy | Never | 5 / 10 | 40% | off |
| Medium (default) | 10 keys | 7 / 10 | 60% | off |
| Hard | 0 keys | 10 / 10 | 80% | on |

---

## How the maths works

All of the game rules live in [`game/core.c`](game/core.c), which never touches raylib, the clock or a random number generator. Time and randomness are passed in, so every rule can be tested without opening a window ([`game/tests/core_tests.c`](game/tests/core_tests.c)).

### 1. Reaction time

When a letter appears, the game notes the time $t_{\text{shown}}$. When you press the right key at $t_{\text{press}}$, your reaction time is:

$$t = t_{\text{press}} - t_{\text{shown}}$$

- **The clock pauses with the settings menu.** The game clock is `real time − total time spent in settings`, so opening settings mid-round doesn't add to your time.
- **Wrong keys don't stop the clock.** The timer keeps running until you hit the right key, so a wrong key costs you the time it takes to notice and correct it.
- **Precision.** Keys are read once per frame and the game runs at 240 frames per second, so any time can be up to $1/240 \approx 4.2$ ms late. Only the first press of a key counts, so holding a key down can't rack up wrong presses.

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

### 3. Letter frequency (the gap between letters)

After you hit a letter, the next one waits for:

$$\text{gap} = (10 - f) \times 0.15\,\text{s} \times u, \qquad u \sim \text{Uniform}(0.7,\ 1.3)$$

where $f$ is the *Letter frequency* slider (1-10). The random factor $u$ changes the gap by up to ±30% so you can't settle into a rhythm and press on the beat.

| Frequency | Average gap | Range |
|---|---|---|
| 10 (Hard) | 0 s | next letter immediately |
| 7 (Medium) | 0.45 s | 0.32 – 0.59 s |
| 5 (Easy) | 0.75 s | 0.53 – 0.98 s |
| 1 | 1.35 s | 0.95 – 1.76 s |

### 4. Random positions

Once you've done the set number of keys (*Random positions after*), each letter is placed at a random point in the play area, to the right of the times list:

- $x$ and $y$ are picked uniformly, with margins so the 160 px letter never touches an edge and there's always room for the timer underneath it.
- The game tries up to 16 random points and takes the first one more than 240 px (1.5 letter heights) from the previous letter. If none are that far, it uses the furthest one it found. So letters always jump somewhere noticeably new, but you can't predict where.

### 5. Flash letter and the wrong-key shake

- **Flash letter:** the letter is drawn only while $t < 0.25$ s. After that just the timer is left, so you have to read the letter in a quarter of a second.
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
  main.c               window, input, drawing, settings
  tests/core_tests.c   tests for core.c
app/                   C++ learning version (see Docs/PLAN.md)
Docs/                  plan, notes and screenshots
CMakeLists.txt         builds both, fetches raylib
```

The game uses Arial from your system (`/System/Library/Fonts/Supplemental/Arial.ttf` on macOS, `C:/Windows/Fonts/arial.ttf` on Windows) and falls back to raylib's built-in font if it isn't there.
