/* main.c - BatakType: a Batak-board style reaction trainer for your keyboard.
 *
 * A letter appears, a timer starts, and you hit that key. 60 seconds per
 * round. Letters you keep getting wrong on the first try come up more often.
 * After 20 keys, letters appear in random places on the screen.
 *
 * Plain look: black background, white Arial text.
 *
 * C11 + raylib 5.5. The rules live in core.c (no raylib). This file is the
 * window, the input and the drawing. Builds natively, or for the browser
 * with Emscripten (see the README).
 */
#include "core.h"
#include "font_data.h"
#include "raylib.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

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
#define NO_GAP_LEVEL 8         /* frequency from which letters follow instantly */
#define GAP_PER_STEP 0.15      /* seconds of gap per frequency step below that */
#define FLASH_AT_8 0.125       /* flash mode: letter time at frequency 8 */
#define FLASH_MAX 1.0          /* flash mode: longest the letter stays up */

static const Color MISS_RED = {255, 40, 40, 255};

/* ------------------------------------------------------------------------ */
/* Settings (the gear menu)                                                  */
/* ------------------------------------------------------------------------ */

typedef struct {
    float roundSeconds;
    float scatterAfter;
    float frequency; /* 1 (slow) .. 10 (fastest): gap, and flash-mode letter time */
    float adaptPercent;
    bool flashLetter; /* letter vanishes after flash_time(), timer keeps going */
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
    {"Easy", SCATTER_NEVER, 2.0f, 40.0f, false},
    {"Medium", 10.0f, 5.0f, 60.0f, false},
    {"Hard", 0.0f, 8.0f, 80.0f, true},
};

/* Start on Medium with a 60 second round. */
static const Settings kDefaultSettings = {60.0f, 10.0f, 5.0f, 60.0f, false};

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
    int letterFrames; /* frames the current letter has been drawn for */

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

/* Real screen pixels per layout pixel. 1 on desktop. In a browser it's the
 * page's devicePixelRatio (2 on a Retina Mac): the canvas is made that many
 * times bigger and everything is drawn scaled up, so it stays sharp instead
 * of being stretched by the browser. Updated every frame. */
static float gScale = 1.0f;

/* Snap a layout coordinate to a whole screen pixel, so text isn't smeared
 * across two pixels. */
static float snap(float v) { return roundf(v * gScale) / gScale; }

/* Window size and mouse position in layout pixels (see gScale). */
static float ui_w(void) { return (float)GetScreenWidth() / gScale; }
static float ui_h(void) { return (float)GetScreenHeight() / gScale; }
static Vector2 ui_mouse(void) {
    Vector2 m = GetMousePosition();
    return (Vector2){m.x / gScale, m.y / gScale};
}

/* The font file: Arial from the system if it's there, otherwise the embedded
 * BatakSans (same letter shapes and widths as Arial). A browser can't read
 * system fonts, so the web version always uses BatakSans. */
static const unsigned char *gFontData = kBatakSansTTF;
static int gFontDataSize = (int)sizeof kBatakSansTTF;
static unsigned char *gArialData = NULL;

static void find_font_data(void) {
#ifndef __EMSCRIPTEN__
    const char *paths[] = {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/arial.ttf",
    };
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        if (!FileExists(paths[i])) continue;
        int n = 0;
        unsigned char *d = LoadFileData(paths[i], &n);
        if (d != NULL && n > 0) {
            gArialData = d;
            gFontData = d;
            gFontDataSize = n;
            return;
        }
        UnloadFileData(d);
    }
#endif
}

/* Each text size gets its own font, rendered at exactly the number of screen
 * pixels it will cover. Every glyph pixel then lands on one screen pixel:
 * nothing is stretched or squashed, so text stays crisp. Fonts are made the
 * first time a size is used and then kept. */
#define FONT_CACHE_MAX 24
typedef struct {
    int px;
    Font font;
} CachedFont;
static CachedFont gFonts[FONT_CACHE_MAX];
static int gFontCount = 0;

static void unload_fonts(void) {
    for (int i = 0; i < gFontCount; i++) UnloadFont(gFonts[i].font);
    gFontCount = 0;
}

static Font font_for(float size) {
    int px = (int)lroundf(size * gScale);
    if (px < 1) px = 1;
    for (int i = 0; i < gFontCount; i++) {
        if (gFonts[i].px == px) return gFonts[i].font;
    }
    if (gFontCount == FONT_CACHE_MAX) unload_fonts();

    /* Only the letter and the countdown are drawn huge: giving those sizes
     * just A-Z and 0-9 keeps their font images small. */
    int bigSet[36];
    int *codepoints = NULL, count = 0;
    if (size >= 100.0f) {
        for (int i = 0; i < 26; i++) bigSet[count++] = 'A' + i;
        for (int i = 0; i < 10; i++) bigSet[count++] = '0' + i;
        codepoints = bigSet;
    }
    Font f = LoadFontFromMemory(".ttf", gFontData, gFontDataSize, px, codepoints, count);
    if (f.texture.id == 0) return GetFontDefault();
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    gFonts[gFontCount++] = (CachedFont){px, f};
    return f;
}

typedef enum { AL_LEFT, AL_CENTER, AL_RIGHT } Align;

static void text(const char *s, float x, float y, float size, Color c, Align al) {
    Font f = font_for(size);
    Vector2 m = MeasureTextEx(f, s, size, 0);
    float dx = (al == AL_CENTER) ? m.x * 0.5f : (al == AL_RIGHT ? m.x : 0.0f);
    DrawTextEx(f, s, (Vector2){snap(x - dx), snap(y)}, size, 0, c);
}

/* Centred on a point, both axes. */
static void text_mid(const char *s, Vector2 c, float size, Color col) {
    Font f = font_for(size);
    Vector2 m = MeasureTextEx(f, s, size, 0);
    DrawTextEx(f, s, (Vector2){snap(c.x - m.x * 0.5f), snap(c.y - m.y * 0.5f)}, size, 0,
               col);
}

/* True for one frame after a left click. Set at the top of frame(): in a
 * browser a quick trackpad tap can go down and up between two frames, so
 * raylib alone would miss it; the web build records clicks as they happen. */
static bool gClicked = false;

/* Outlined text button, filled white when hovered or selected.
 * hovered == NULL means drawn but not clickable. */
static bool button_sel(Rectangle r, const char *label, bool *hovered,
                       bool selected) {
    bool hot = hovered != NULL && CheckCollisionPointRec(ui_mouse(), r);
    if (hot) *hovered = true;
    if (hot || selected) {
        DrawRectangleRec(r, WHITE);
    } else {
        DrawRectangleLinesEx(r, 1.0f, WHITE);
    }
    text_mid(label, (Vector2){r.x + r.width / 2, r.y + r.height / 2}, 18,
             (hot || selected) ? BLACK : WHITE);
    return hot && gClicked;
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
    float w = ui_w(), h = ui_h();
    return (Rectangle){SIDE_W, TOP_H, w - SIDE_W, h - TOP_H};
}

static Vector2 to_screen(Rectangle P, Vector2 norm) {
    return (Vector2){P.x + norm.x * P.width, P.y + norm.y * P.height};
}

/* Centred horizontally on the whole window (like the title), yFrac of the
 * way down the area below the top line. */
static Vector2 screen_center(Rectangle P, float yFrac) {
    return (Vector2){ui_w() * 0.5f, P.y + P.height * yFrac};
}

static Vector2 letter_pos(Rectangle P) {
    return g.scattered ? to_screen(P, g.pos) : screen_center(P, 0.42f);
}

static Vector2 gear_center(void) {
    return (Vector2){ui_w() - 40.0f, 38.0f};
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
 * 8 and above = straight away, each step below 8 adds GAP_PER_STEP seconds
 * (5 = ~0.45 s, 1 = ~1.05 s). A little randomness (+/-30%) stops you
 * settling into a rhythm. */
static double next_gap(void) {
    double steps = NO_GAP_LEVEL - gSet.frequency;
    if (steps < 0.0) steps = 0.0;
    return steps * GAP_PER_STEP * (0.7 + 0.6 * rand01());
}

/* Shortest time the screen can show something: one refresh. Set in main(). */
static double gOneRefresh = 1.0 / 60.0;

/* Flash mode: how long the letter stays up, from the same slider.
 * 8 = 0.125 s; each step up halves it, each step down doubles it (up to 1 s).
 * 10 = as short as possible: a single screen refresh. */
static double flash_time(void) {
    int f = (int)gSet.frequency;
    if (f >= 10) return gOneRefresh;
    double t = FLASH_AT_8 * pow(2.0, (double)(NO_GAP_LEVEL - f));
    return t < FLASH_MAX ? t : FLASH_MAX;
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
    g.letterFrames = 0;
    g.shakeUntil = -1.0;
}

static void on_key(char c, double now) {
    if (!g.live) return; /* between letters: ignored */

    if (c == (char)('A' + g.letter)) {
        double t = now - g.shownAt;
        if (t < 0.0) t = 0.0;
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
    float h = ui_h();
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
    Vector2 c = {ui_w() * 0.5f, ui_h() * 0.45f};
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

    /* flash mode: the letter only shows briefly, the timer stays. It's
     * always drawn on its first frame, however short the flash time. */
    if (!gSet.flashLetter || el < flash_time() || g.letterFrames == 0) {
        char s[2] = {(char)('A' + g.letter), 0};
        text_mid(s, c, LETTER_SIZE, WHITE);
    }
    g.letterFrames++;

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
    return (Rectangle){ui_w() - SET_W - 20, 70, SET_W, h};
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
    Vector2 m = ui_mouse();

    if (CheckCollisionPointRec(m, checkbox_row(P))) {
        *hover = true;
        if (gClicked) {
            gSet.flashLetter = !gSet.flashLetter;
        }
    }

    for (int i = 0; i < SLIDER_COUNT; i++) {
        if (CheckCollisionPointRec(m, slider_hitbox(slider_track(P, i)))) {
            *hover = true;
            if (gClicked) gActiveSlider = i;
        }
    }
    if (gActiveSlider < 0) return;

    /* move the knob to the mouse: on the click itself, then while dragging */
    *hover = true;
    Slider *s = &gSliders[gActiveSlider];
    Rectangle tr = slider_track(P, gActiveSlider);
    float t = clampf((m.x - tr.x) / tr.width, 0.0f, 1.0f);
    float v = roundf((s->min + t * (s->max - s->min)) / s->step) * s->step;
    *s->value = clampf(v, s->min, s->max);
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) gActiveSlider = -1;
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
/* Browser support                                                           */
/* ------------------------------------------------------------------------ */

#ifdef __EMSCRIPTEN__
/* In a browser the game only redraws at the screen's refresh rate (usually
 * 60 Hz), so reading keys once per frame would round every reaction time to
 * the nearest ~17 ms. Instead we listen for key presses directly and note
 * the exact moment each one happened, then hand them to the game with that
 * timestamp. Only the first press counts: held-down repeats are ignored. */
typedef struct {
    char letter;
    double ms; /* emscripten_get_now() at the moment of the key press */
} WebKey;

#define WEB_KEY_MAX 64
static WebKey gWebKeys[WEB_KEY_MAX];
static int gWebKeyCount = 0;

static EM_BOOL on_web_keydown(int type, const EmscriptenKeyboardEvent *e,
                              void *user) {
    (void)type;
    (void)user;
    /* e->code is the physical key ("KeyA".."KeyZ"), like raylib's key codes */
    bool letter = strlen(e->code) == 4 && strncmp(e->code, "Key", 3) == 0 &&
                  e->code[3] >= 'A' && e->code[3] <= 'Z';
    if (letter && !e->repeat && gWebKeyCount < WEB_KEY_MAX) {
        gWebKeys[gWebKeyCount++] = (WebKey){e->code[3], emscripten_get_now()};
    }
    return EM_FALSE; /* don't swallow it: raylib still sees every key */
}

static bool gWebClick = false;

static EM_BOOL on_web_mousedown(int type, const EmscriptenMouseEvent *e,
                                void *user) {
    (void)type;
    (void)user;
    if (e->button == 0) gWebClick = true;
    return EM_FALSE;
}

/* Keep the canvas covering the browser window, with one canvas pixel per
 * real screen pixel (the page's CSS keeps its on-screen size). */
static void fit_canvas_to_page(void) {
    double dpr = emscripten_get_device_pixel_ratio();
    gScale = (float)(dpr >= 1.0 && dpr <= 4.0 ? dpr : 1.0);
    double w = 0, h = 0;
    emscripten_get_element_css_size("#canvas", &w, &h);
    int pw = (int)lround(w * gScale), ph = (int)lround(h * gScale);
    if (pw >= 1 && ph >= 1 && (pw != GetScreenWidth() || ph != GetScreenHeight())) {
        SetWindowSize(pw, ph);
    }
}
#endif

/* ------------------------------------------------------------------------ */
/* Main                                                                      */
/* ------------------------------------------------------------------------ */

/* One frame: input, update, draw. Called in a loop on desktop, and by the
 * browser once per screen refresh on the web. */
static void frame(void) {
#ifdef __EMSCRIPTEN__
    fit_canvas_to_page();
    gClicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || gWebClick;
    gWebClick = false;
#else
    gClicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
#endif
    Rectangle P = play_rect();
    double now = game_clock();
    bool hover = false;
    /* If settings were open at the start of this frame, the click that
     * closes them must not also press a button underneath. */
    bool uiBlocked = gSettingsOpen;

    /* ---- input ---- */
    Vector2 gc = gear_center();
    bool gearHot = CheckCollisionPointCircle(ui_mouse(), gc, 24);
    if (gearHot) hover = true;
    if (gearHot && gClicked) {
        toggle_settings();
    } else if (gSettingsOpen && gClicked &&
               !CheckCollisionPointRec(ui_mouse(), settings_rect())) {
        toggle_settings(); /* click outside closes */
    }

#ifdef __EMSCRIPTEN__
    /* letters, with the exact time each key went down (see on_web_keydown) */
    double pageToRaylib = emscripten_get_now() / 1000.0 - GetTime();
    for (int i = 0; i < gWebKeyCount; i++) {
        if (gSettingsOpen || g.state != ST_PLAYING) continue;
        double t = gWebKeys[i].ms / 1000.0 - pageToRaylib - gPausedTotal;
        on_key(gWebKeys[i].letter, t);
    }
    gWebKeyCount = 0;
    const bool lettersFromFrame = false;
#else
    const bool lettersFromFrame = true;
#endif

    /* raylib's key queue only reports the first press, so holding a key
     * down can't rack up misses. */
    int key;
    while ((key = GetKeyPressed()) != 0) {
        if (key == KEY_ESCAPE) {
            if (gSettingsOpen) {
                toggle_settings();
            } else if (g.state != ST_MENU) {
                g.state = ST_MENU;
                g.live = false;
            }
            continue;
        }
        if (gSettingsOpen) continue;
        if (key >= KEY_A && key <= KEY_Z && g.state == ST_PLAYING) {
            if (lettersFromFrame) on_key((char)('A' + (key - KEY_A)), now);
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
    /* draw in layout pixels; the camera scales them up to screen pixels */
    Camera2D cam = {{0, 0}, {0, 0}, 0.0f, gScale};
    BeginMode2D(cam);

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

    EndMode2D();
    EndDrawing();
    SetMouseCursor(hover ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 800, "BatakType");
    SetExitKey(KEY_NULL); /* Esc is used in-game; close with the window button */
    SetRandomSeed((unsigned int)time(NULL));
#ifdef __EMSCRIPTEN__
    /* the browser draws once per screen refresh, so "first frame only" is
     * already a single refresh */
    gOneRefresh = 0.0;
#else
    int hz = GetMonitorRefreshRate(GetCurrentMonitor());
    gOneRefresh = 1.0 / (hz >= 30 ? hz : 60);
#endif

    find_font_data();

    gSet = kDefaultSettings;
    init_sliders();
    memset(&g, 0, sizeof g);
    g.state = ST_MENU;
    g.scattered = false;
    g.prevLetter = -1;

#ifdef __EMSCRIPTEN__
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE,
                                    on_web_keydown);
    emscripten_set_mousedown_callback("#canvas", NULL, EM_TRUE, on_web_mousedown);
    emscripten_set_main_loop(frame, 0, 1); /* 0 = the browser's refresh rate */
#else
    SetWindowMinSize(960, 640);
    SetTargetFPS(240); /* keys are read once per frame: ~4 ms resolution */
    while (!WindowShouldClose()) {
        frame();
    }
#endif

    unload_fonts();
    if (gArialData != NULL) UnloadFileData(gArialData);
    CloseWindow();
    return 0;
}
