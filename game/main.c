/* main.c - BatakType: a Batak-board style reaction trainer for your keyboard.
 *
 * A letter appears, a timer starts, and you hit that key. 60 seconds per
 * round. Letters you keep getting wrong on the first try come up more often.
 * After 20 keys, letters appear in random places on the screen.
 *
 * Plain look: black background, white Arial text.
 *
 * C11 + raylib 5.5. The rules live in core.c (no raylib). This file is the
 * window, the input and the drawing.
 */
#include "core.h"
#include "raylib.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------------ */
/* Tuning that isn't on a slider                                             */
/* ------------------------------------------------------------------------ */

#define MAX_SPLITS 2048
#define COUNT_STEP 0.7         /* seconds per countdown number (3, 2, 1) */
#define SHAKE_TIME 0.35        /* how long the timer vibrates after a miss */
#define FIRST_LETTER_DELAY 0.3 /* pause after the countdown */
#define SIDE_W 240.0f          /* width of the times list on the left */
#define TOP_H 70.0f
#define LETTER_SIZE 160.0f     /* letter height in px */
#define SHAKE_PX 14.0f         /* how far the timer shakes on a wrong key */
#define FLASH_TIME 0.25        /* flash mode: how long the letter stays up */
#define GAP_PER_STEP 0.15      /* seconds of gap per step below max frequency */

static const Color MISS_RED = {255, 40, 40, 255};

/* ------------------------------------------------------------------------ */
/* Settings (the gear menu)                                                  */
/* ------------------------------------------------------------------------ */

typedef struct {
    float roundSeconds;
    float scatterAfter;
    float frequency; /* 1 (slow) .. 10 (next letter straight away) */
    float adaptPercent;
    bool flashLetter; /* letter vanishes after FLASH_TIME, timer keeps going */
} Settings;

/* "Random positions after" at its maximum means never. */
#define SCATTER_NEVER 100.0f

/* Difficulty presets. Each sets everything except the round length. */
typedef struct {
    const char *name;
    float scatterAfter;
    float frequency;
    float adaptPercent;
    bool flashLetter;
} Preset;

#define PRESET_COUNT 3
static const Preset kPresets[PRESET_COUNT] = {
    {"Easy", SCATTER_NEVER, 5.0f, 40.0f, false},
    {"Medium", 10.0f, 7.0f, 60.0f, false},
    {"Hard", 0.0f, 10.0f, 80.0f, true},
};

/* Start on Medium with a 60 second round. */
static const Settings kDefaultSettings = {60.0f, 10.0f, 7.0f, 60.0f, false};

typedef struct {
    const char *label;
    const char *fmt;
    float *value;
    float min, max, step;
    const char *maxLabel; /* shown instead of the number at max, or NULL */
} Slider;

#define SLIDER_COUNT 4
static Settings gSet;
static Slider gSliders[SLIDER_COUNT];
static int gActiveSlider = -1;
static bool gSettingsOpen = false;

static void init_sliders(void) {
    Slider s[SLIDER_COUNT] = {
        {"Round length", "%.0f s", &gSet.roundSeconds, 10, 180, 5, NULL},
        {"Random positions after", "%.0f keys", &gSet.scatterAfter, 0,
         SCATTER_NEVER, 1, "Never"},
        {"Letter frequency", "%.0f / 10", &gSet.frequency, 1, 10, 1, NULL},
        {"Focus on missed letters", "%.0f%%", &gSet.adaptPercent, 0, 100, 5, NULL},
    };
    memcpy(gSliders, s, sizeof s);
}

static void apply_preset(int i) {
    gSet.scatterAfter = kPresets[i].scatterAfter;
    gSet.frequency = kPresets[i].frequency;
    gSet.adaptPercent = kPresets[i].adaptPercent;
    gSet.flashLetter = kPresets[i].flashLetter;
}

/* Which preset the current settings match, or -1 if they've been tweaked. */
static int current_preset(void) {
    for (int i = 0; i < PRESET_COUNT; i++) {
        const Preset *p = &kPresets[i];
        if (gSet.scatterAfter == p->scatterAfter && gSet.frequency == p->frequency &&
            gSet.adaptPercent == p->adaptPercent && gSet.flashLetter == p->flashLetter) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------------ */
/* Game state                                                                */
/* ------------------------------------------------------------------------ */

typedef enum { ST_MENU, ST_COUNTDOWN, ST_PLAYING, ST_RESULTS } State;

typedef struct {
    State state;
    double countdownStart;
    double roundStart;
    double nextLetterAt;
    double shownAt;
    bool live; /* a letter is showing and the timer is running */
    int letter;
    int prevLetter;
    int misses;     /* wrong presses on the current letter */
    double shakeUntil;
    bool scattered; /* letter is at a random spot instead of the centre */
    Vector2 pos;    /* random spot, normalised inside the play area */
    int keysDone;

    Split splits[MAX_SPLITS];
    int splitCount;
    LetterStats stats[LETTER_COUNT]; /* kept across rounds */

    RoundSummary summary;
    char troubleLetter[6];
    int troubleMisses[6];
    int troubleCount;
} Game;

static Game g;

/* Game clock: real time minus time spent paused in the settings menu. */
static double gPausedTotal = 0.0;
static double gPauseStart = 0.0;
static bool gPaused = false;

static double game_clock(void) {
    return gPaused ? gPauseStart - gPausedTotal : GetTime() - gPausedTotal;
}

static void set_paused(bool p) {
    if (p && !gPaused) {
        gPauseStart = GetTime();
        gPaused = true;
    } else if (!p && gPaused) {
        gPausedTotal += GetTime() - gPauseStart;
        gPaused = false;
    }
}

/* ------------------------------------------------------------------------ */
/* Font and text                                                             */
/* ------------------------------------------------------------------------ */

static Font gBig;   /* the letter and the timer */
static Font gSmall; /* everything else */
static bool gOwnBig = false, gOwnSmall = false;

/* Arial ships with macOS and Windows; this looks in the usual places and
 * falls back to raylib's built-in font if it isn't found. */
static Font load_arial(int size, bool *owned) {
    const char *paths[] = {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/arial.ttf",
    };
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        if (!FileExists(paths[i])) continue;
        Font f = LoadFontEx(paths[i], size, NULL, 0);
        if (f.texture.id != 0) {
            GenTextureMipmaps(&f.texture);
            SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
            *owned = true;
            return f;
        }
    }
    *owned = false;
    return GetFontDefault();
}

static Font font_for(float size) { return size >= 48.0f ? gBig : gSmall; }

typedef enum { AL_LEFT, AL_CENTER, AL_RIGHT } Align;

static void text(const char *s, float x, float y, float size, Color c, Align al) {
    Font f = font_for(size);
    Vector2 m = MeasureTextEx(f, s, size, 0);
    float dx = (al == AL_CENTER) ? m.x * 0.5f : (al == AL_RIGHT ? m.x : 0.0f);
    DrawTextEx(f, s, (Vector2){roundf(x - dx), roundf(y)}, size, 0, c);
}

/* Centred on a point, both axes. */
static void text_mid(const char *s, Vector2 c, float size, Color col) {
    Font f = font_for(size);
    Vector2 m = MeasureTextEx(f, s, size, 0);
    DrawTextEx(f, s, (Vector2){c.x - m.x * 0.5f, c.y - m.y * 0.5f}, size, 0, col);
}

/* Outlined text button, filled white when hovered or selected.
 * hovered == NULL means drawn but not clickable. */
static bool button_sel(Rectangle r, const char *label, bool *hovered,
                       bool selected) {
    bool hot = hovered != NULL && CheckCollisionPointRec(GetMousePosition(), r);
    if (hot) *hovered = true;
    if (hot || selected) {
        DrawRectangleRec(r, WHITE);
    } else {
        DrawRectangleLinesEx(r, 1.0f, WHITE);
    }
    text_mid(label, (Vector2){r.x + r.width / 2, r.y + r.height / 2}, 18,
             (hot || selected) ? BLACK : WHITE);
    return hot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static bool button(Rectangle r, const char *label, bool *hovered) {
    return button_sel(r, label, hovered, false);
}

/* ------------------------------------------------------------------------ */
/* Layout                                                                    */
/* ------------------------------------------------------------------------ */

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static double rand01(void) {
    return (double)GetRandomValue(0, 999999) / 1000000.0;
}

static Rectangle play_rect(void) {
    float w = (float)GetScreenWidth(), h = (float)GetScreenHeight();
    return (Rectangle){SIDE_W, TOP_H, w - SIDE_W, h - TOP_H};
}

static Vector2 to_screen(Rectangle P, Vector2 norm) {
    return (Vector2){P.x + norm.x * P.width, P.y + norm.y * P.height};
}

/* Centred horizontally on the whole window (like the title), yFrac of the
 * way down the area below the top line. */
static Vector2 screen_center(Rectangle P, float yFrac) {
    return (Vector2){(float)GetScreenWidth() * 0.5f, P.y + P.height * yFrac};
}

static Vector2 letter_pos(Rectangle P) {
    return g.scattered ? to_screen(P, g.pos) : screen_center(P, 0.42f);
}

static Vector2 gear_center(void) {
    return (Vector2){(float)GetScreenWidth() - 40.0f, 38.0f};
}

/* A random spot for the letter that leaves room for the timer underneath
 * and isn't right on top of the previous one. */
static Vector2 random_pos(Rectangle P, Vector2 avoidScreen) {
    float half = LETTER_SIZE * 0.5f;
    float minX = half + 30, maxX = P.width - half - 30;
    float minY = half + 20, maxY = P.height - half - 90;
    if (maxX < minX) maxX = minX;
    if (maxY < minY) maxY = minY;

    Vector2 avoid = {avoidScreen.x - P.x, avoidScreen.y - P.y};
    Vector2 best = {minX, minY};
    float bestDist = -1.0f;
    for (int i = 0; i < 16; i++) {
        Vector2 p = {minX + (float)rand01() * (maxX - minX),
                     minY + (float)rand01() * (maxY - minY)};
        float dx = p.x - avoid.x, dy = p.y - avoid.y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d > bestDist) {
            best = p;
            bestDist = d;
        }
        if (d > LETTER_SIZE * 1.5f) break;
    }
    return (Vector2){best.x / P.width, best.y / P.height};
}

/* ------------------------------------------------------------------------ */
/* Game flow                                                                 */
/* ------------------------------------------------------------------------ */

static void start_round(void) {
    g.state = ST_COUNTDOWN;
    g.countdownStart = game_clock();
    g.splitCount = 0;
    g.keysDone = 0;
    g.live = false;
    g.scattered = false;
    g.prevLetter = -1;
}

static void finish_round(void) {
    g.state = ST_RESULTS;
    g.live = false;
    g.summary = summarize(g.splits, g.splitCount);
    g.troubleCount = round_trouble(g.splits, g.splitCount, g.troubleLetter,
                                   g.troubleMisses, 6);
}

/* Pause before the next letter, from the Letter frequency slider:
 * 10 = straight away, each step down adds GAP_PER_STEP seconds (1 = ~1.35 s).
 * A little randomness (+/-30%) stops you settling into a rhythm. */
static double next_gap(void) {
    double base = (10.0 - gSet.frequency) * GAP_PER_STEP;
    return base * (0.7 + 0.6 * rand01());
}

static void show_next_letter(double now, Rectangle P) {
    g.letter = pick_letter(g.stats, gSet.adaptPercent / 100.0, g.prevLetter,
                           rand01());
    if (gSet.scatterAfter < SCATTER_NEVER && g.keysDone >= (int)gSet.scatterAfter) {
        g.pos = random_pos(P, letter_pos(P));
        g.scattered = true;
    } else {
        g.scattered = false;
    }
    g.live = true;
    g.shownAt = now;
    g.misses = 0;
    g.shakeUntil = -1.0;
}

static void on_key(char c, double now) {
    if (!g.live) return; /* between letters: ignored */

    if (c == (char)('A' + g.letter)) {
        double t = now - g.shownAt;
        if (g.splitCount < MAX_SPLITS) {
            g.splits[g.splitCount++] = (Split){c, t, g.misses};
        }
        LetterStats *s = &g.stats[g.letter];
        s->attempts++;
        s->hits++;
        s->totalTime += t;
        if (g.misses > 0) s->firstTryMisses++;

        g.keysDone++;
        g.live = false;
        g.prevLetter = g.letter;
        g.nextLetterAt = now + next_gap();
    } else {
        g.misses++;
        g.shakeUntil = now + SHAKE_TIME;
    }
}

static void update_game(double now, Rectangle P) {
    if (g.state == ST_COUNTDOWN) {
        if (now - g.countdownStart >= 3.0 * COUNT_STEP) {
            g.state = ST_PLAYING;
            g.roundStart = now;
            g.nextLetterAt = now + FIRST_LETTER_DELAY;
        }
    } else if (g.state == ST_PLAYING) {
        if (now - g.roundStart >= gSet.roundSeconds) {
            finish_round();
            return;
        }
        if (!g.live && now >= g.nextLetterAt) {
            show_next_letter(now, P);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Drawing                                                                   */
/* ------------------------------------------------------------------------ */

static void draw_times_list(void) {
    float h = (float)GetScreenHeight();
    text("Times", 24, 24, 22, WHITE, AL_LEFT);

    const float rowH = 24.0f, top = 64.0f, footer = 90.0f;
    int maxRows = (int)((h - top - footer) / rowH);
    if (maxRows < 0) maxRows = 0;

    /* newest at the top */
    int shown = g.splitCount < maxRows ? g.splitCount : maxRows;
    for (int k = 0; k < shown; k++) {
        int idx = g.splitCount - 1 - k;
        const Split *s = &g.splits[idx];
        float y = top + (float)k * rowH;
        char left[48];
        if (s->misses > 0) {
            snprintf(left, sizeof left, "%d.  %c  (%d wrong)", idx + 1, s->letter,
                     s->misses);
        } else {
            snprintf(left, sizeof left, "%d.  %c", idx + 1, s->letter);
        }
        text(left, 24, y, 18, WHITE, AL_LEFT);
        text(TextFormat("%.3f", s->time), SIDE_W - 24, y, 18, WHITE, AL_RIGHT);
    }

    RoundSummary sum = summarize(g.splits, g.splitCount);
    text("Total", 24, h - footer + 20, 18, WHITE, AL_LEFT);
    text(TextFormat("%.3f s", sum.total), 24, h - footer + 42, 30, WHITE, AL_LEFT);
}

static void draw_top(double now) {
    double remaining = gSet.roundSeconds;
    if (g.state == ST_PLAYING) {
        remaining = gSet.roundSeconds - (now - g.roundStart);
        if (remaining < 0) remaining = 0;
    } else if (g.state == ST_RESULTS) {
        remaining = 0;
    }
    text(TextFormat("Time left: %.1f", remaining), SIDE_W + 24, 24, 22, WHITE,
         AL_LEFT);
    text(TextFormat("Keys: %d", g.splitCount), SIDE_W + 260, 24, 22, WHITE,
         AL_LEFT);
}

static void draw_gear(Vector2 c, float r) {
    for (int i = 0; i < 4; i++) {
        DrawRectanglePro((Rectangle){c.x, c.y, r * 0.46f, r * 2.0f},
                         (Vector2){r * 0.23f, r}, (float)i * 45.0f, WHITE);
    }
    DrawCircleV(c, r * 0.76f, WHITE);
    DrawCircleV(c, r * 0.34f, BLACK);
}

/* The menu is centred on the whole window, not just the play area. */
static void draw_menu(bool *hover) {
    Vector2 c = {(float)GetScreenWidth() * 0.5f, (float)GetScreenHeight() * 0.45f};
    text_mid("BatakType", (Vector2){c.x, c.y - 60}, 64, WHITE);
    text_mid("A letter appears. Press that key as fast as you can.",
             (Vector2){c.x, c.y + 10}, 20, WHITE);
    Rectangle b = {c.x - 90, c.y + 50, 180, 44};
    if (button(b, "Start", hover)) start_round();
    text_mid("or press Space", (Vector2){c.x, c.y + 120}, 16, WHITE);
}

static void draw_countdown(Rectangle P, double now) {
    int n = 3 - (int)((now - g.countdownStart) / COUNT_STEP);
    if (n < 1) n = 1;
    char s[2] = {(char)('0' + n), 0};
    text_mid(s, screen_center(P, 0.42f), LETTER_SIZE, WHITE);
}

static void draw_playing(Rectangle P, double now) {
    if (!g.live) return;
    Vector2 c = letter_pos(P);
    double el = now - g.shownAt;

    /* flash mode: the letter only shows briefly, the timer stays */
    if (!gSet.flashLetter || el < FLASH_TIME) {
        char s[2] = {(char)('A' + g.letter), 0};
        text_mid(s, c, LETTER_SIZE, WHITE);
    }

    /* timer under the letter: red after a wrong key, shakes for a moment */
    Vector2 tp = {c.x, c.y + LETTER_SIZE * 0.5f + 40};
    if (now < g.shakeUntil) {
        float k = (float)((g.shakeUntil - now) / SHAKE_TIME);
        float amp = SHAKE_PX * k * k;
        tp.x += sinf((float)now * 95.0f) * amp;
        tp.y += cosf((float)now * 71.0f) * amp * 0.35f;
    }
    text_mid(TextFormat("%.3f", el), tp, 48, g.misses > 0 ? MISS_RED : WHITE);
}

static void draw_results(Rectangle P, bool *hover) {
    RoundSummary *s = &g.summary;
    Vector2 c = screen_center(P, 0.38f);

    text_mid("Total time", (Vector2){c.x, c.y - 110}, 24, WHITE);
    text_mid(TextFormat("%.3f s", s->total), (Vector2){c.x, c.y - 50}, 80, WHITE);

    char line[160];
    if (s->count > 0) {
        snprintf(line, sizeof line,
                 "Keys: %d     Average: %.3f     Best: %.3f (%c)     Slowest: %.3f (%c)",
                 s->count, s->average, s->best, s->bestLetter, s->worst,
                 s->worstLetter);
    } else {
        snprintf(line, sizeof line, "No keys hit this round.");
    }
    text_mid(line, (Vector2){c.x, c.y + 30}, 20, WHITE);

    if (s->count > 0) {
        if (g.troubleCount == 0) {
            snprintf(line, sizeof line, "First try: 100%%. No wrong keys.");
        } else {
            int n = snprintf(line, sizeof line, "First try: %.0f%%     Missed most:",
                             100.0 * s->cleanHits / s->count);
            for (int i = 0; i < g.troubleCount && n < (int)sizeof line - 12; i++) {
                n += snprintf(line + n, sizeof line - (size_t)n, "  %c x%d",
                              g.troubleLetter[i], g.troubleMisses[i]);
            }
        }
        text_mid(line, (Vector2){c.x, c.y + 64}, 20, WHITE);
    }

    Rectangle again = {c.x - 190, c.y + 120, 180, 44};
    Rectangle menu = {c.x + 10, c.y + 120, 180, 44};
    if (button(again, "Play again", hover)) start_round();
    if (button(menu, "Menu", hover)) g.state = ST_MENU;
    text_mid("Space to play again, Esc for menu", (Vector2){c.x, c.y + 190}, 16,
             WHITE);
}

/* ------------------------------------------------------------------------ */
/* Settings panel                                                            */
/* ------------------------------------------------------------------------ */

#define SET_W 420.0f
#define SET_ROW 56.0f

#define CHECK_ROW 48.0f
#define PRESET_Y 66.0f   /* preset buttons, below the title */
#define SLIDERS_Y 124.0f /* first slider row */

static Rectangle settings_rect(void) {
    float h = SLIDERS_Y + SLIDER_COUNT * SET_ROW + CHECK_ROW + 80.0f;
    return (Rectangle){(float)GetScreenWidth() - SET_W - 20, 70, SET_W, h};
}

static Rectangle slider_track(Rectangle P, int i) {
    float y = P.y + SLIDERS_Y + (float)i * SET_ROW;
    return (Rectangle){P.x + 24, y + 32, P.width - 48, 2};
}

static Rectangle slider_hitbox(Rectangle tr) {
    return (Rectangle){tr.x - 12, tr.y - 16, tr.width + 24, tr.height + 32};
}

/* The "Flash letter" checkbox row, under the sliders. */
static Rectangle checkbox_row(Rectangle P) {
    float y = P.y + SLIDERS_Y + SLIDER_COUNT * SET_ROW + 4.0f;
    return (Rectangle){P.x + 24, y, P.width - 48, 28};
}

/* Slider dragging. Drawing happens in draw_settings. */
static void settings_update(bool *hover) {
    Rectangle P = settings_rect();
    Vector2 m = GetMousePosition();

    if (CheckCollisionPointRec(m, checkbox_row(P))) {
        *hover = true;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            gSet.flashLetter = !gSet.flashLetter;
        }
    }

    for (int i = 0; i < SLIDER_COUNT; i++) {
        if (CheckCollisionPointRec(m, slider_hitbox(slider_track(P, i)))) {
            *hover = true;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gActiveSlider = i;
        }
    }
    if (gActiveSlider < 0) return;

    *hover = true;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        gActiveSlider = -1;
        return;
    }
    Slider *s = &gSliders[gActiveSlider];
    Rectangle tr = slider_track(P, gActiveSlider);
    float t = clampf((m.x - tr.x) / tr.width, 0.0f, 1.0f);
    float v = roundf((s->min + t * (s->max - s->min)) / s->step) * s->step;
    *s->value = clampf(v, s->min, s->max);
}

static void toggle_settings(void) {
    gSettingsOpen = !gSettingsOpen;
    gActiveSlider = -1;
    set_paused(gSettingsOpen);
}

static void draw_settings(bool *hover) {
    Rectangle P = settings_rect();
    DrawRectangleRec(P, BLACK);
    DrawRectangleLinesEx(P, 1.0f, WHITE);

    text("Settings", P.x + 24, P.y + 22, 24, WHITE, AL_LEFT);
    if (g.state == ST_PLAYING || g.state == ST_COUNTDOWN) {
        text("(paused)", P.x + P.width - 24, P.y + 26, 18, WHITE, AL_RIGHT);
    }

    /* presets: the one matching the current settings is filled in */
    int cur = current_preset();
    float pw = (P.width - 48 - 20) / 3;
    for (int i = 0; i < PRESET_COUNT; i++) {
        Rectangle r = {P.x + 24 + (float)i * (pw + 10), P.y + PRESET_Y, pw, 36};
        if (button_sel(r, kPresets[i].name, hover, i == cur)) apply_preset(i);
    }

    for (int i = 0; i < SLIDER_COUNT; i++) {
        Slider *s = &gSliders[i];
        Rectangle tr = slider_track(P, i);
        float frac = (*s->value - s->min) / (s->max - s->min);
        text(s->label, tr.x, tr.y - 28, 18, WHITE, AL_LEFT);
        const char *val = (s->maxLabel && *s->value >= s->max)
                              ? s->maxLabel
                              : TextFormat(s->fmt, *s->value);
        text(val, tr.x + tr.width, tr.y - 28, 18, WHITE, AL_RIGHT);
        DrawRectangleRec(tr, GRAY);
        DrawRectangleRec((Rectangle){tr.x, tr.y, tr.width * frac, tr.height}, WHITE);
        DrawCircleV((Vector2){tr.x + tr.width * frac, tr.y + 1},
                    gActiveSlider == i ? 9.0f : 7.0f, WHITE);
    }

    Rectangle cb = checkbox_row(P);
    Rectangle box = {cb.x, cb.y + 4, 20, 20};
    DrawRectangleLinesEx(box, 1.5f, WHITE);
    if (gSet.flashLetter) {
        DrawLineEx((Vector2){box.x + 4, box.y + 10}, (Vector2){box.x + 8, box.y + 15},
                   2.5f, WHITE);
        DrawLineEx((Vector2){box.x + 8, box.y + 15}, (Vector2){box.x + 16, box.y + 5},
                   2.5f, WHITE);
    }
    text("Flash letter (hide it, keep the timer)", box.x + 32, cb.y + 5, 18, WHITE,
         AL_LEFT);

    float by = P.y + P.height - 62;
    float bw = (P.width - 48 - 10) / 2;
    if (button((Rectangle){P.x + 24, by, bw, 40}, "Reset focus", hover)) {
        memset(g.stats, 0, sizeof g.stats);
    }
    if (button((Rectangle){P.x + 24 + bw + 10, by, bw, 40}, "Done", hover)) {
        toggle_settings();
    }
}

/* ------------------------------------------------------------------------ */
/* Main                                                                      */
/* ------------------------------------------------------------------------ */

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 800, "BatakType");
    SetWindowMinSize(960, 640);
    SetExitKey(KEY_NULL); /* Esc is used in-game; close with the window button */
    SetTargetFPS(240);    /* input is sampled per frame: ~4 ms resolution */
    SetRandomSeed((unsigned int)time(NULL));

    gBig = load_arial(256, &gOwnBig);
    gSmall = load_arial(48, &gOwnSmall);

    gSet = kDefaultSettings;
    init_sliders();
    memset(&g, 0, sizeof g);
    g.state = ST_MENU;
    g.scattered = false;
    g.prevLetter = -1;

    while (!WindowShouldClose()) {
        Rectangle P = play_rect();
        double now = game_clock();
        bool hover = false;
        /* If settings were open at the start of this frame, the click that
         * closes them must not also press a button underneath. */
        bool uiBlocked = gSettingsOpen;

        /* ---- input ---- */
        Vector2 gc = gear_center();
        bool gearHot = CheckCollisionPointCircle(GetMousePosition(), gc, 24);
        if (gearHot) hover = true;
        if (gearHot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            toggle_settings();
        } else if (gSettingsOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                   !CheckCollisionPointRec(GetMousePosition(), settings_rect())) {
            toggle_settings(); /* click outside closes */
        }

        if (IsKeyPressed(KEY_ESCAPE)) {
            if (gSettingsOpen) {
                toggle_settings();
            } else if (g.state != ST_MENU) {
                g.state = ST_MENU;
                g.live = false;
            }
        }

        /* raylib's key queue only reports the first press, so holding a key
         * down can't rack up misses. */
        int key;
        while ((key = GetKeyPressed()) != 0) {
            if (gSettingsOpen) continue;
            if (key >= KEY_A && key <= KEY_Z && g.state == ST_PLAYING) {
                on_key((char)('A' + (key - KEY_A)), now);
            } else if ((key == KEY_SPACE || key == KEY_ENTER) &&
                       (g.state == ST_MENU || g.state == ST_RESULTS)) {
                start_round();
            }
        }

        if (gSettingsOpen) settings_update(&hover);

        /* ---- update ---- */
        now = game_clock();
        if (!gSettingsOpen) update_game(now, P);

        /* ---- draw ---- */
        BeginDrawing();
        ClearBackground(BLACK);

        bool *playHover = (uiBlocked || gSettingsOpen) ? NULL : &hover;
        switch (g.state) {
        case ST_MENU: draw_menu(playHover); break;
        case ST_COUNTDOWN: draw_countdown(P, now); break;
        case ST_PLAYING: draw_playing(P, now); break;
        case ST_RESULTS: draw_results(P, playHover); break;
        }
        draw_times_list();
        draw_top(now);
        draw_gear(gc, 15.0f);
        if (gSettingsOpen) draw_settings(&hover);

        EndDrawing();
        SetMouseCursor(hover ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
    }

    if (gOwnBig) UnloadFont(gBig);
    if (gOwnSmall) UnloadFont(gSmall);
    CloseWindow();
    return 0;
}
