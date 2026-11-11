# BatakType 1.0: how the maths works

[← Back to the README](../README.md)

This page explains every formula behind the game: how reaction times are measured, how the next letter is picked, how the speed scale and Competitive schedule work, and how a round becomes a throughput in bits per second. Play it at <https://samuelojo.tech/projects/batak-type/>.

## Contents

1. [Reaction time](#1-reaction-time)
2. [Picking the next letter](#2-picking-the-next-letter-focus-on-missed-letters)
3. [Letter frequency: gap and flash time](#3-letter-frequency-gap-and-flash-time), including [why the limit depends on your monitor](#the-limit-depends-on-your-monitor)
4. [Random positions](#4-random-positions)
5. [Flash letter and the wrong-key shake](#5-flash-letter-and-the-wrong-key-shake)
6. [End-of-round stats](#6-end-of-round-stats)
7. [Competitive speed schedule](#7-competitive-speed-schedule)
8. [Information throughput: bits per second](#8-information-throughput-bits-per-second)

All of the game rules live in [`game/core.c`](../game/core.c), which never touches raylib, the clock or a random number generator. Time and randomness are passed in, so every rule can be tested without opening a window ([`game/tests/core_tests.c`](../game/tests/core_tests.c)).

## 1. Reaction time

When a letter appears, the game notes the time $t_{\text{shown}}$. When you press the right key at $t_{\text{press}}$, your reaction time is:

$$t = t_{\text{press}} - t_{\text{shown}}$$

- **The clock pauses with the settings menu.** The game clock is `real time − total time spent in settings`, so opening settings mid-round doesn't add to your time.
- **Wrong keys don't stop the clock.** The timer keeps running until you hit the right key, so a wrong key costs you the time it takes to notice and correct it.
- **Precision on desktop.** Keys are read once per frame and the game runs at 240 frames per second, so any time can be up to $1/240 \approx 4.2$ ms late.
- **Precision in a browser.** Browsers only redraw at the screen's refresh rate (usually 60 Hz), so reading keys once per frame would round every time to the nearest $1/60 \approx 16.7$ ms. Instead, the web build listens for key presses directly and notes the exact moment each one happened (`emscripten_get_now()`, accurate to about 0.1 ms in Chrome and 1 ms in Firefox and Safari). That timestamp is converted onto the game clock, so $t_{\text{press}}$ is when your finger hit the key, not when the next frame happened to run.
- Only the first press of a key counts, so holding a key down can't rack up wrong presses.

## 2. Picking the next letter (focus on missed letters)

Every letter keeps a running record across rounds:

- $a$ = how many times you've completed it
- $m$ = how many of those times you pressed a wrong key first (a "first-try miss")

The letter's **weight** is:

$$w = 1 + s \cdot 8 \cdot \frac{m}{a + 2}$$

where $s$ is the *Focus on missed letters* slider as a fraction (60% → $0.6$), and 8 is `FOCUS_GAIN` in `core.h`.

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

## 3. Letter frequency: gap and flash time

The *Letter frequency* slider, $f$, runs from 1 up to a top level that depends on your monitor (15 on a 60 Hz screen, 18 at 120 Hz). It sets two things, and level 8 is the reference point.

**Gap between letters.** After you hit a letter, the next one waits for:

$$\text{gap} = \max(0,\ 8 - f) \times 0.15\,\text{s} \times u, \qquad u \sim \text{Uniform}(0.7,\ 1.3)$$

So from 8 upwards the next letter comes immediately, and each step below 8 adds 0.15 s. The random factor $u$ changes the gap by up to ±30% so you can't settle into a rhythm and press on the beat.

**Flash time** (when *Flash letter* is ticked, and always in Competitive). How long the letter stays on screen before only the timer is left:

$$\text{flash} = 0.12\,\text{s} \times 0.75^{\,f - 8}, \qquad \text{kept between one screen refresh and 1 s}$$

Each level up shows the letter for 25% less time, and each level down for a third longer.

| Frequency | Average gap | Flash time |
|---|---|---|
| 1 | 1.05 s | 899 ms |
| 5 (Easy) | 0.45 s | 284 ms (Easy has flash off) |
| 6 (Medium) | 0.30 s | 213 ms |
| 7 | 0.15 s | 160 ms |
| 8 (Hard) | 0 s | 120 ms |
| 9 | 0 s | 90 ms |
| 10 | 0 s | 67.5 ms |
| 11 | 0 s | 51 ms |
| 12 | 0 s | 38 ms |
| 13 | 0 s | 28 ms |
| 14 | 0 s | 21 ms |
| 15 | 0 s | 16.0 ms |
| 16 | 0 s | 12.0 ms |
| 17 | 0 s | 9.0 ms |
| 18 | 0 s | 6.8 ms |
| 19 | 0 s | 5.1 ms |
| 20 | 0 s | 3.8 ms |

With *Flash letter* off, every level from 8 up plays the same: there is no gap left to remove, and the letter stays until you hit it.

### The limit depends on your monitor

A monitor redraws the picture a fixed number of times a second, so nothing can be on screen for less than one refresh: 16.7 ms at 60 Hz, 8.3 ms at 120 Hz. A game could fake a shorter flash by drawing the letter dimmer. BatakType doesn't: the letter is always drawn at full brightness, and the slider simply stops at the first level whose flash is as short as one refresh of your monitor:

$$f_{\max} = \text{the smallest } f \text{ with } \ 0.12\,\text{s} \times 0.75^{\,f - 8} \ \le\ \frac{1}{\text{refresh rate}}$$

At 60 Hz that is level 15: $0.12 \times 0.75^{7} = 16.0$ ms, just inside one 16.7 ms refresh.

| Monitor | One refresh | Top of the slider |
|---|---|---|
| 60 Hz | 16.7 ms | 15 |
| 75 Hz | 13.3 ms | 16 |
| 90 Hz | 11.1 ms | 17 |
| 120 Hz | 8.3 ms | 18 |
| 144 Hz | 6.9 ms | 18 |
| 240 Hz | 4.2 ms | 20 |

At the top level the letter is drawn for exactly one frame, and whatever the setting, every letter is drawn on at least one frame, so it can never be skipped.

That is right at the edge of what people can read. In rapid-image experiments, viewers could still identify what they had seen from exposures as short as about 13 ms (Potter et al., 2014). One caveat: a single bright letter on a black screen lingers in your vision for a moment after it's gone, which makes it easier to catch than that figure suggests.

**How the game knows your refresh rate.** The desktop version asks the operating system. A browser won't say, but it runs the game exactly once per refresh, so the web version times batches of 31 frames, takes the middle value (which ignores the odd slow frame) and matches it to the nearest standard rate.

## 4. Random positions

Once you've done the set number of keys (*Random positions after*), each letter is placed at a random point in the play area, to the right of the times list:

- $x$ and $y$ are picked uniformly, with margins so the 160 px letter never touches an edge and there's always room for the timer underneath it.
- The game tries up to 16 random points and takes the first one more than 240 px (1.5 letter heights) from the previous letter. If none are that far, it uses the furthest one it found. So letters always jump somewhere noticeably new, but you can't predict where.

## 5. Flash letter and the wrong-key shake

- **Flash letter:** the letter is drawn only while $t$ is under the flash time from section 3 (0.12 s on Hard). After that just the timer is left, so you have to read the letter in that moment.
- **Shake:** a wrong key starts a 0.35 s shake. With $k$ = the fraction of that time left (1 down to 0), the timer moves by

$$\Delta x = 14\,k^2 \sin(95\,t), \qquad \Delta y = 0.35 \cdot 14\,k^2 \cos(71\,t)$$

The $k^2$ makes it start hard and settle quickly. Using two different frequencies (95 and 71) makes it look like a rattle rather than a smooth circle.

## 6. End-of-round stats

For the $n$ letters you completed in a round, with reaction times $t_1 \dots t_n$:

| Stat | Formula |
|---|---|
| Total time | $\sum t_i$ |
| Average | $\frac{1}{n}\sum t_i$ (a round with no hits is handled separately, so it never divides by zero) |
| Best / Slowest | $\min t_i$ / $\max t_i$, with the letter |
| First try | $\frac{\text{letters with no wrong presses}}{n} \times 100\%$ |
| Missed most | letters with first-try misses this round, sorted by how many times |
| Throughput | $\frac{\sum H_i}{\sum t_i}$ bits per second (see below) |

A letter that's still on screen when the round ends isn't counted.

## 7. Competitive speed schedule

Competitive starts at level 5. The first speed-up comes after 5 seconds, and each wait after that is 1.4 times longer than the last, rounded to a whole second: 5, 7, 10, 14, 19, 27, 38... That is a geometric series, so the time to reach step $k$ is close to

$$T_k \approx 5 \cdot \frac{1.4^{k} - 1}{1.4 - 1}\ \text{s}$$

The early steps come quickly, and every later one asks you to hold a faster pace for longer. Each step shortens the gap between letters and, because letters always flash in Competitive, cuts the time to read each one (section 3):

| Time survived | Speed | Gap | Letter on screen |
|---|---|---|---|
| 0 s | 5 | 0.45 s | 284 ms |
| 5 s | 6 | 0.30 s | 213 ms |
| 12 s | 7 | 0.15 s | 160 ms |
| 22 s | 8 | 0 s | 120 ms |
| 36 s | 9 | 0 s | 90 ms |
| 55 s | 10 | 0 s | 67.5 ms |
| 82 s | 11 | 0 s | 51 ms |
| 120 s | 12 | 0 s | 38 ms |
| 173 s | 13 | 0 s | 28 ms |
| 247 s | 14 | 0 s | 21 ms |
| 350 s | 15 | 0 s | 16 ms (one refresh at 60 Hz) |

The speed keeps climbing until it reaches the top level for your monitor, then stays there. One wrong key, or 5 seconds on a letter, ends the run, and the score is the number of letters you got right.

## 8. Information throughput: bits per second

Each letter you react to is a choice the game made, and information theory measures how much information that choice carries, in bits. If the game picks letter $i$ with probability $p_i$, the information in one pick is its Shannon entropy:

$$H = -\sum_i p_i \log_2 p_i$$

- With every letter equally likely, that's $\log_2 26 \approx 4.70$ bits. The game never repeats the previous letter, so there are really 25 options: $\log_2 25 \approx 4.64$ bits. That's what every Competitive pick carries.
- In Casual, *Focus on missed letters* tilts the odds towards letters you fumble. That makes the next letter more predictable, so each pick carries slightly **less** than 4.64 bits.

The game calculates $H$ from the real probabilities just before every pick (`pick_entropy()` in `core.c`) and stores it with that letter's time. Throughput is the total information divided by the total time:

$$\text{throughput} = \frac{\sum_i H_i}{\sum_i t_i} \ \text{bits/s}$$

A 0.35 s average in Competitive is $4.64 / 0.35 \approx 13.3$ bits/s; 0.30 s is about 15.5 bits/s. It's the same measure used for pointing speed in human-computer interaction research. It also makes rounds with different focus settings fair to compare: a more predictable game gives you fewer bits per letter.

