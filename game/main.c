/* BatakType 1.0 - main.c: the game itself.
 *
 * A letter appears, a timer starts, and you hit that key as fast as you can.
 *
 * The code is split into three parts:
 *   core.c  the rules and the maths (no drawing, fully tested)
 *   ui.c    sharp text, buttons, the mouse and the screen
 *   main.c  this file: what the game remembers, how a round flows,
 *           what each screen looks like, and the loop that runs it all
 *
 * This file from top to bottom:
 *   1. Numbers you can tune
 *   2. Settings and presets
 *   3. What the game remembers
 *   4. The game clock (which can pause)
 *   5. Where things go on screen
 *   6. How a round flows
 *   7. Drawing the screens
 *   8. The settings panel
 *   9. Keyboard input
 *  10. One frame, and main()
 *
 * Reading tips: names starting with "g" (gSet, gGame...) are this file's
 * long-lived variables. "static" means "private to this file".
 * Code between "#ifdef __EMSCRIPTEN__" and "#endif" is browser-only.
 */
#include "core.h"
#include "raylib.h"
#include "ui.h"

#include <math.h>   /* sinf, cosf, sqrtf, roundf */
#include <stdio.h>  /* snprintf: builds a piece of text from values */
#include <string.h> /* memset, strlen, strncmp */
#include <time.h>   /* time: seeds the random numbers */

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

#define VERSION "1.01"

/* ======================================================================== */
/* 1. Numbers you can tune                                                   */
/* ======================================================================== */

#define MAX_SPLITS 4096        /* most letters one round can record */
#define COUNTDOWN_STEP 1.0     /* seconds each of "3, 2, 1" stays up */
#define FIRST_LETTER_DELAY 0.3 /* pause between the countdown and letter 1 */
#define TIMEOUT_GENTLE 1.0     /* Casual on Easy or Medium: seconds to hit each letter */
#define TIMEOUT_STRICT 0.5     /* Casual on Hard, and all of Competitive */
#define CASUAL_MAX_MISSES 3    /* Casual: wrong presses on ONE letter that end it */
#define SHAKE_TIME 0.35        /* how long the timer shakes after a wrong key */
#define SHAKE_DISTANCE 14.0f   /* how far it shakes, in pixels */
#define LETTER_SIZE 160.0f     /* height of the big letter */
#define SIDEBAR_WIDTH 240.0f   /* the "Times" list down the left */
#define TOP_BAR_HEIGHT 70.0f
#define SCATTER_NEVER 100.0f   /* top of the "Random positions after" slider */

static const Color MISS_RED = {255, 40, 40, 255}; /* red, green, blue, opacity */

/* ======================================================================== */
/* 2. Settings and presets                                                   */
/* ======================================================================== */

/* Everything the settings panel can change. Casual mode uses all of it.
 * Competitive only uses scatterAfter; it sets the rest itself. */
typedef struct {
    float roundSeconds; /* how long a Casual round lasts */
    float scatterAfter; /* letters done before they start moving around */
    float frequency;    /* the speed level: see the speed scale in core.h */
    float focusPercent; /* 0-100: how hard to bring back missed letters */
    bool flashLetter;   /* true = the letter vanishes after a moment */
} Settings;

/* A difficulty preset: one button that sets everything but round length. */
typedef struct {
    const char *name;
    float scatterAfter, frequency, focusPercent;
    bool flashLetter;
} Preset;

#define PRESET_COUNT 3
static const Preset kPresets[PRESET_COUNT] = {
    /* name    scatter  freq  focus  flash */
    {"Easy",   10.0f,   5.0f, 60.0f, false},
    {"Medium",  5.0f,   6.0f, 70.0f, true},
    {"Hard",    0.0f,   8.0f, 80.0f, true},
};

/* The settings in use. The game starts on Easy with a 60 second round. */
static Settings gSet = {60.0f, 10.0f, 5.0f, 60.0f, false};

static void apply_preset(int i) {
    gSet.scatterAfter = kPresets[i].scatterAfter;
    gSet.frequency = kPresets[i].frequency;
    gSet.focusPercent = kPresets[i].focusPercent;
    gSet.flashLetter = kPresets[i].flashLetter;
}

/* Which preset the settings currently match, or -1 if you've changed them. */
static int current_preset(void) {
    for (int i = 0; i < PRESET_COUNT; i++) {
        const Preset *p = &kPresets[i];
        if (gSet.scatterAfter == p->scatterAfter && gSet.frequency == p->frequency &&
            gSet.focusPercent == p->focusPercent && gSet.flashLetter == p->flashLetter) {
            return i;
        }
    }
    return -1;
}

/* ======================================================================== */
/* 3. What the game remembers                                                */
/* ======================================================================== */

/* Which screen is showing. An "enum" is a list of named choices. */
typedef enum { SCREEN_MENU, SCREEN_COUNTDOWN, SCREEN_PLAYING, SCREEN_RESULTS } Screen;

/* Which game is being played. */
typedef enum { MODE_CASUAL, MODE_COMPETITIVE } Mode;

typedef struct {
    Screen screen;
    Mode mode;
    double countdownStart; /* game-clock time the "3, 2, 1" began */
    double roundStart;     /* game-clock time the round began */

    /* --- the letter on screen right now --- */
    bool live;           /* true while a letter is waiting to be pressed */
    int letter;          /* 0 = A ... 25 = Z */
    int previousLetter;  /* so the same letter never comes twice running */
    double shownAt;      /* when it appeared */
    double nextLetterAt; /* when the next one is due (while !live) */
    int misses;          /* wrong keys pressed on this letter so far */
    double shakeUntil;   /* the timer shakes until this time */
    double bits;         /* information this pick carried */
    int framesDrawn;     /* frames this letter has been drawn for */
    bool scattered;      /* true = at a random spot, false = centred */
    Vector2 spot;        /* that random spot, as fractions of the play area */

    /* --- this round --- */
    Split splits[MAX_SPLITS]; /* every letter completed, in order */
    int splitCount;

    /* --- filled in when a round ends, for the results screen --- */
    RoundSummary summary;
    char troubleLetters[6]; /* letters missed most this round */
    int troubleMisses[6];
    int troubleCount;
    double survived;     /* seconds the round lasted */
    int levelReached;    /* speed level when it ended */
    char endReason[96];  /* why it ended early, or empty */

    /* --- kept until you close the game --- */
    LetterStats stats[LETTER_COUNT]; /* your record on each letter */
    int bestCompetitive;             /* most letters in a Competitive run */
    bool seenSettings;               /* the settings panel has been opened */
} Game;

static Game gGame; /* starts as all zeros: menu screen, Casual mode */

static bool gSettingsOpen = false;

/* ======================================================================== */
/* 4. The game clock (which can pause)                                       */
/* ======================================================================== */

/* All gameplay timing uses the game clock: real time minus any time spent
 * with the settings panel open. So opening settings freezes everything. */
static double gPausedTotal = 0.0; /* total seconds spent paused so far */
static double gPauseStart = 0.0;  /* when the current pause began */
static bool gPaused = false;

/* Converts a real time (seconds since the game started) to game-clock time. */
static double to_game_clock(double realTime) {
    return (gPaused ? gPauseStart : realTime) - gPausedTotal;
}

static double game_clock(void) { return to_game_clock(GetTime()); }

static void set_paused(bool paused) {
    if (paused && !gPaused) {
        gPauseStart = GetTime();
    } else if (!paused && gPaused) {
        gPausedTotal += GetTime() - gPauseStart;
    }
    gPaused = paused;
}

static void open_settings(void) {
    gSettingsOpen = true;
    gGame.seenSettings = true;
    set_paused(true);
}

static void close_settings(void) {
    gSettingsOpen = false;
    set_paused(false);
}

/* ======================================================================== */
/* 5. Where things go on screen                                              */
/* ======================================================================== */

/* A random number from 0.0 up to (not including) 1.0. */
static double random01(void) { return GetRandomValue(0, 999999) / 1000000.0; }

/* The area letters can appear in: right of the sidebar, below the top bar. */
static Rectangle play_area(void) {
    return (Rectangle){SIDEBAR_WIDTH, TOP_BAR_HEIGHT, ui_width() - SIDEBAR_WIDTH,
                       ui_height() - TOP_BAR_HEIGHT};
}

/* A point in the middle of the whole window, "down" of the way down the
 * play area (0.0 = top, 1.0 = bottom). */
static Vector2 centre_point(float down) {
    Rectangle area = play_area();
    return (Vector2){ui_width() * 0.5f, area.y + area.height * down};
}

/* Where the current letter is drawn. */
static Vector2 letter_point(void) {
    if (!gGame.scattered) return centre_point(0.42f);
    Rectangle area = play_area();
    return (Vector2){area.x + gGame.spot.x * area.width,
                     area.y + gGame.spot.y * area.height};
}

/* Picks a random spot for the next letter and stores it in gGame.spot.
 * It keeps clear of the edges, leaves room for the timer underneath, and
 * tries to land well away from the previous letter. */
static void choose_random_spot(void) {
    Rectangle area = play_area();
    Vector2 previous = letter_point();
    float half = LETTER_SIZE * 0.5f;
    float left = area.x + half + 30, right = area.x + area.width - half - 30;
    float top = area.y + half + 20, bottom = area.y + area.height - half - 90;
    if (right < left) right = left; /* window too small: don't go backwards */
    if (bottom < top) bottom = top;

    /* Try up to 16 random points. Take the first that's far enough from
     * the previous letter; failing that, the farthest one we found. */
    Vector2 best = {left, top};
    float bestDistance = -1.0f;
    for (int attempt = 0; attempt < 16; attempt++) {
        Vector2 p = {left + (float)random01() * (right - left),
                     top + (float)random01() * (bottom - top)};
        float dx = p.x - previous.x, dy = p.y - previous.y;
        float distance = sqrtf(dx * dx + dy * dy); /* Pythagoras */
        if (distance > bestDistance) {
            best = p;
            bestDistance = distance;
        }
        if (distance > LETTER_SIZE * 1.5f) break; /* far enough */
    }
    /* Stored as fractions so it stays in place if the window is resized. */
    gGame.spot = (Vector2){(best.x - area.x) / area.width,
                           (best.y - area.y) / area.height};
}

/* ======================================================================== */
/* 6. How a round flows                                                      */
/* ======================================================================== */

/* The highest speed level this monitor can show (see core.c max_level). */
static int top_level(void) { return max_level(ui_refresh_seconds()); }

/* The three things Competitive mode decides for itself. */
static int speed_level(double now) {
    if (gGame.mode == MODE_COMPETITIVE) {
        return competitive_level(now - gGame.roundStart, top_level());
    }
    return (int)gSet.frequency;
}
static bool flash_is_on(void) {
    return gGame.mode == MODE_COMPETITIVE || gSet.flashLetter;
}
static double focus(void) {
    return gGame.mode == MODE_COMPETITIVE ? 0.0 : gSet.focusPercent / 100.0;
}

/* Begins a new round with the "3, 2, 1" countdown. */
static void start_round(Mode mode) {
    gGame.mode = mode;
    gGame.screen = SCREEN_COUNTDOWN;
    gGame.countdownStart = game_clock();
    gGame.splitCount = 0;
    gGame.live = false;
    gGame.scattered = false;
    gGame.previousLetter = -1; /* -1 = "there wasn't one" */
    gGame.endReason[0] = 0;    /* an empty piece of text */
}

/* Ends the round and works out everything the results screen shows.
 * "reason" explains an early ending; pass "" for a normal one. */
static void finish_round(double now, const char *reason) {
    gGame.survived = now - gGame.roundStart;
    gGame.levelReached = speed_level(now);
    snprintf(gGame.endReason, sizeof gGame.endReason, "%s", reason);

    gGame.summary = summarize(gGame.splits, gGame.splitCount);
    gGame.troubleCount = round_trouble(gGame.splits, gGame.splitCount,
                                       gGame.troubleLetters, gGame.troubleMisses, 6);
    if (gGame.mode == MODE_COMPETITIVE && gGame.splitCount > gGame.bestCompetitive) {
        gGame.bestCompetitive = gGame.splitCount;
    }
    gGame.screen = SCREEN_RESULTS;
    gGame.live = false;
}

/* Puts the next letter on screen and starts its timer. */
static void show_next_letter(double now) {
    /* Measure the information in this pick before making it. */
    gGame.bits = pick_entropy(gGame.stats, focus(), gGame.previousLetter);
    gGame.letter = pick_letter(gGame.stats, focus(), gGame.previousLetter, random01());

    gGame.scattered = gSet.scatterAfter < SCATTER_NEVER &&
                      gGame.splitCount >= (int)gSet.scatterAfter;
    if (gGame.scattered) choose_random_spot();

    gGame.live = true;
    gGame.shownAt = now;
    gGame.misses = 0;
    gGame.framesDrawn = 0;
    gGame.shakeUntil = -1.0;
}

/* Called for every letter key pressed. "now" is the moment it went down. */
static void press_letter(char key, double now) {
    if (gSettingsOpen || gGame.screen != SCREEN_PLAYING || !gGame.live) {
        return; /* nothing to hit: ignore the press */
    }
    char wanted = (char)('A' + gGame.letter);

    if (key != wanted) {
        /* Wrong key. The timer keeps running; it turns red and shakes. */
        gGame.misses++;
        gGame.shakeUntil = now + SHAKE_TIME;
        if (gGame.mode == MODE_COMPETITIVE) {
            finish_round(now, TextFormat("Wrong key: pressed %c, the letter was %c",
                                         key, wanted));
        } else if (gGame.misses >= CASUAL_MAX_MISSES) {
            finish_round(now, TextFormat("Ended early: %d wrong presses on %c",
                                         CASUAL_MAX_MISSES, wanted));
        }
        return;
    }

    /* Right key: record the reaction time. */
    double reaction = now - gGame.shownAt;
    if (reaction < 0.0) reaction = 0.0;
    if (gGame.splitCount < MAX_SPLITS) {
        gGame.splits[gGame.splitCount++] =
            (Split){wanted, reaction, gGame.misses, gGame.bits};
    }

    /* Update your record on this letter, which feeds the picker. */
    LetterStats *record = &gGame.stats[gGame.letter];
    record->attempts++;
    if (gGame.misses > 0) record->firstTryMisses++;

    gGame.live = false;
    gGame.previousLetter = gGame.letter;
    gGame.nextLetterAt = now + gap_seconds(speed_level(now), random01());
}

/* How long a letter can wait for its key before the round ends. Casual on
 * Easy or Medium gives a second; Casual on Hard and all of Competitive give
 * half a second. If you've moved the sliders yourself (no preset matches),
 * Hard's letter frequency or faster counts as Hard. */
static double letter_timeout(void) {
    if (gGame.mode == MODE_COMPETITIVE) return TIMEOUT_STRICT;
    int preset = current_preset();
    bool hard = (preset >= 0) ? (preset == PRESET_COUNT - 1)
                              : (gSet.frequency >= kPresets[PRESET_COUNT - 1].frequency);
    return hard ? TIMEOUT_STRICT : TIMEOUT_GENTLE;
}

/* Moves the round along. Called once per frame while settings are closed. */
static void update_round(double now) {
    if (gGame.screen == SCREEN_COUNTDOWN) {
        if (now - gGame.countdownStart >= 3 * COUNTDOWN_STEP) {
            gGame.screen = SCREEN_PLAYING;
            gGame.roundStart = now;
            gGame.nextLetterAt = now + FIRST_LETTER_DELAY;
        }
        return;
    }
    if (gGame.screen != SCREEN_PLAYING) return;

    /* Casual: the round is over when the time runs out. */
    if (gGame.mode == MODE_CASUAL && now - gGame.roundStart >= gSet.roundSeconds) {
        finish_round(now, "");
        return;
    }
    /* Both modes: a letter left too long ends the round (see letter_timeout). */
    double timeout = letter_timeout();
    if (gGame.live && now - gGame.shownAt >= timeout) {
        finish_round(now, TextFormat("%s: %c wasn't pressed within %.1f s",
                                     gGame.mode == MODE_COMPETITIVE ? "Too slow"
                                                                    : "Ended early",
                                     'A' + gGame.letter, timeout));
        return;
    }
    if (!gGame.live && now >= gGame.nextLetterAt) {
        show_next_letter(now);
    }
}

/* ======================================================================== */
/* 7. Drawing the screens                                                    */
/* ======================================================================== */

/* The list of times down the left, newest at the top, with the total. */
static void draw_sidebar(void) {
    const float rowHeight = 24, firstRow = 64, footer = 90;
    ui_text("Times", 24, 24, 22, WHITE, ALIGN_LEFT);

    int rows = (int)((ui_height() - firstRow - footer) / rowHeight); /* that fit */
    for (int row = 0; row < rows && row < gGame.splitCount; row++) {
        int index = gGame.splitCount - 1 - row; /* newest first */
        const Split *s = &gGame.splits[index];
        float y = firstRow + row * rowHeight;
        const char *label = (s->misses > 0)
            ? TextFormat("%d.  %c  (%d wrong)", index + 1, s->letter, s->misses)
            : TextFormat("%d.  %c", index + 1, s->letter);
        ui_text(label, 24, y, 18, WHITE, ALIGN_LEFT);
        ui_text(TextFormat("%.3f", s->time), SIDEBAR_WIDTH - 24, y, 18, WHITE,
                ALIGN_RIGHT);
    }

    double total = summarize(gGame.splits, gGame.splitCount).total;
    ui_text("Total", 24, ui_height() - footer + 20, 18, WHITE, ALIGN_LEFT);
    ui_text(TextFormat("%.3f s", total), 24, ui_height() - footer + 42, 30, WHITE,
            ALIGN_LEFT);
}

/* The line of information along the top. */
static void draw_top_bar(double now) {
    float x = SIDEBAR_WIDTH + 24;
    bool playing = gGame.screen == SCREEN_PLAYING;
    bool finished = gGame.screen == SCREEN_RESULTS;

    if (gGame.mode == MODE_COMPETITIVE && gGame.screen != SCREEN_MENU) {
        /* Competitive counts up, and shows the speed level. */
        double time = playing ? now - gGame.roundStart : (finished ? gGame.survived : 0.0);
        int level = playing ? speed_level(now)
                            : (finished ? gGame.levelReached : COMPETITIVE_START_LEVEL);
        ui_text(TextFormat("Competitive   Time: %.1f", time), x, 24, 22, WHITE, ALIGN_LEFT);
        ui_text(TextFormat("Speed: %d / %d", level, top_level()), x + 336, 24, 22,
                WHITE, ALIGN_LEFT);
        ui_text(TextFormat("Keys: %d", gGame.splitCount), x + 516, 24, 22, WHITE,
                ALIGN_LEFT);
        return;
    }

    /* Casual counts down. */
    double left = gSet.roundSeconds;
    if (playing) left = fmax(0.0, gSet.roundSeconds - (now - gGame.roundStart));
    if (finished) left = 0.0;
    ui_text(TextFormat("Time left: %.1f", left), x, 24, 22, WHITE, ALIGN_LEFT);
    ui_text(TextFormat("Keys: %d", gGame.splitCount), x + 236, 24, 22, WHITE, ALIGN_LEFT);
}

/* The settings button: a gear in the top right corner. */
static Rectangle gear_area(void) {
    return (Rectangle){ui_width() - 64, 14, 48, 48};
}

static void draw_gear(void) {
    Rectangle area = gear_area();
    Vector2 c = {area.x + area.width / 2, area.y + area.height / 2};
    float r = 15.0f;
    /* Four bars crossed at 45 degree steps make the eight teeth... */
    for (int i = 0; i < 4; i++) {
        DrawRectanglePro((Rectangle){c.x, c.y, r * 0.46f, r * 2.0f},
                         (Vector2){r * 0.23f, r}, i * 45.0f, WHITE);
    }
    DrawCircleV(c, r * 0.76f, WHITE); /* ...a disc makes the body... */
    DrawCircleV(c, r * 0.34f, BLACK); /* ...and a black dot makes the hole. */
}

#ifdef __EMSCRIPTEN__
/* Browser only: a fullscreen button just left of the gear. A browser only
 * allows fullscreen in direct response to a click, so the switch happens
 * inside the click event itself (on_web_fullscreen_click), not in frame(). */
EM_JS(int, web_fullscreen_supported, (void), {
    return (document.fullscreenEnabled || document.webkitFullscreenEnabled) ? 1 : 0;
})
EM_JS(int, web_is_fullscreen, (void), {
    return (document.fullscreenElement || document.webkitFullscreenElement) ? 1 : 0;
})
EM_JS(void, web_toggle_fullscreen, (void), {
    var d = document, page = d.documentElement;
    if (d.fullscreenElement || d.webkitFullscreenElement) {
        (d.exitFullscreen || d.webkitExitFullscreen).call(d);
    } else {
        var request = page.requestFullscreen || page.webkitRequestFullscreen;
        if (request) {
            var result = request.call(page);
            if (result && result.catch) result.catch(function () {});
        }
    }
})

static bool gFullscreenSupported = false; /* false on e.g. iPhone Safari */

static Rectangle fullscreen_area(void) {
    Rectangle gear = gear_area();
    return (Rectangle){gear.x - gear.width - 4, gear.y, gear.width, gear.height};
}

/* Four corner brackets: pointing out to go fullscreen, in to leave it. */
static void draw_fullscreen_button(void) {
    if (!gFullscreenSupported) return;
    Rectangle area = fullscreen_area();
    ui_over(area); /* hand cursor on hover */
    Vector2 c = {area.x + area.width / 2, area.y + area.height / 2};
    const float half = 13.0f, arm = 8.0f, thick = 3.0f;
    bool full = web_is_fullscreen();
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            /* The corner of each bracket, and the way its two arms point. */
            float reach = full ? half - arm : half;
            Vector2 corner = {c.x + sx * reach, c.y + sy * reach};
            float dx = full ? (float)sx : (float)-sx;
            float dy = full ? (float)sy : (float)-sy;
            float x0 = fminf(corner.x, corner.x + dx * arm);
            float y0 = fminf(corner.y, corner.y + dy * arm);
            DrawRectangleRec((Rectangle){x0, corner.y - thick / 2, arm, thick}, WHITE);
            DrawRectangleRec((Rectangle){corner.x - thick / 2, y0, thick, arm}, WHITE);
        }
    }
}

static EM_BOOL on_web_fullscreen_click(int type, const EmscriptenMouseEvent *event,
                                       void *user) {
    (void)type;
    (void)user;
    /* The canvas fills the page from its top-left corner, so page
     * coordinates are the game's own coordinates. */
    Vector2 point = {(float)event->clientX, (float)event->clientY};
    if (event->button == 0 && gFullscreenSupported &&
        CheckCollisionPointRec(point, fullscreen_area())) {
        web_toggle_fullscreen();
    }
    return EM_FALSE; /* let the game see the click too */
}
#endif

static void draw_menu(void) {
    Vector2 c = {ui_width() * 0.5f, ui_height() * 0.45f};
    ui_text_centered("BatakType", (Vector2){c.x, c.y - 60}, 64, WHITE);
    ui_text_centered("A letter appears. Press that key as fast as you can.",
                     (Vector2){c.x, c.y + 10}, 20, WHITE);

    Rectangle casual = {c.x - 190, c.y + 50, 180, 44};
    Rectangle competitive = {c.x + 10, c.y + 50, 180, 44};
    if (ui_button(casual, "Casual", false)) {
        /* The first time, show the settings so you can pick a difficulty.
         * Pressing Done there starts the round. */
        if (gGame.seenSettings) start_round(MODE_CASUAL);
        else open_settings();
    }
    if (ui_button(competitive, "Competitive", false)) {
        start_round(MODE_COMPETITIVE);
    }
    ui_text_centered("Competitive: one wrong key or half a second on a letter and "
                     "you're out. It keeps getting faster.",
                     (Vector2){c.x, c.y + 120}, 16, WHITE);
    ui_text("Version " VERSION, ui_width() - 24, ui_height() - 36, 16, WHITE,
            ALIGN_RIGHT);
}

static void draw_countdown(double now) {
    int number = 3 - (int)((now - gGame.countdownStart) / COUNTDOWN_STEP);
    if (number < 1) number = 1;
    ui_text_centered(TextFormat("%d", number), centre_point(0.42f), LETTER_SIZE, WHITE);
}

/* Should the letter be drawn this frame? (The timer always is.) */
static bool letter_is_visible(double elapsed, double now) {
    if (!flash_is_on()) return true;            /* it stays until you hit it */
    if (gGame.framesDrawn == 0) return true;    /* always shown at least once */

    double refresh = ui_refresh_seconds();
    double flash = flash_seconds(speed_level(now), refresh);
#ifdef __EMSCRIPTEN__
    /* A browser draws exactly one frame per refresh, so this rounds the
     * flash to the nearest whole number of frames. */
    flash -= refresh / 2;
#endif
    return elapsed < flash;
}

static void draw_playing(double now) {
    if (!gGame.live) return; /* between letters: an empty screen */
    Vector2 c = letter_point();
    double elapsed = now - gGame.shownAt;

    if (letter_is_visible(elapsed, now)) {
        ui_text_centered(TextFormat("%c", 'A' + gGame.letter), c, LETTER_SIZE, WHITE);
    }
    gGame.framesDrawn++;

    /* The timer under the letter. After a wrong key it is red, and for a
     * moment it shakes: hard at first, then settling (that's the k * k). */
    Vector2 timer = {c.x, c.y + LETTER_SIZE * 0.5f + 40};
    if (now < gGame.shakeUntil) {
        float k = (float)((gGame.shakeUntil - now) / SHAKE_TIME); /* 1 down to 0 */
        float strength = SHAKE_DISTANCE * k * k;
        timer.x += sinf((float)now * 95.0f) * strength;
        timer.y += cosf((float)now * 71.0f) * strength * 0.35f;
    }
    ui_text_centered(TextFormat("%.3f", elapsed), timer, 48,
                     gGame.misses > 0 ? MISS_RED : WHITE);
}

static void draw_results(void) {
    const RoundSummary *s = &gGame.summary;
    Vector2 c = centre_point(0.36f);
    char line[200]; /* room to build one line of text */

    /* The headline: letters correct (Competitive) or average (Casual). */
    if (gGame.mode == MODE_COMPETITIVE) {
        ui_text_centered("Letters correct", (Vector2){c.x, c.y - 120}, 24, WHITE);
        ui_text_centered(TextFormat("%d", s->count), (Vector2){c.x, c.y - 60}, 80, WHITE);
    } else {
        ui_text_centered("Average reaction", (Vector2){c.x, c.y - 120}, 24, WHITE);
        ui_text_centered(s->count > 0 ? TextFormat("%.3f s", s->average) : "-",
                         (Vector2){c.x, c.y - 60}, 80, WHITE);
    }
    ui_text_centered(gGame.endReason, (Vector2){c.x, c.y + 4}, 20, MISS_RED);

    /* First line of detail. */
    if (gGame.mode == MODE_COMPETITIVE) {
        snprintf(line, sizeof line,
                 "Survived: %.3f s     Speed reached: %d / %d     Best this session: %d",
                 gGame.survived, gGame.levelReached, top_level(), gGame.bestCompetitive);
    } else if (s->count > 0) {
        snprintf(line, sizeof line,
                 "Total time: %.3f s     Keys: %d     Best: %.3f (%c)     Slowest: %.3f (%c)",
                 s->total, s->count, s->best, s->bestLetter, s->worst, s->worstLetter);
    } else {
        snprintf(line, sizeof line, "No keys hit this round.");
    }
    ui_text_centered(line, (Vector2){c.x, c.y + 40}, 20, WHITE);

    /* Second line of detail (only if there is anything to say). */
    if (s->count > 0) {
        if (gGame.mode == MODE_COMPETITIVE) {
            snprintf(line, sizeof line,
                     "Average: %.3f     Best: %.3f (%c)     Throughput: %.1f bits/s",
                     s->average, s->best, s->bestLetter, s->throughput);
        } else {
            int used = snprintf(line, sizeof line,
                                "Throughput: %.1f bits/s     First try: %.0f%%",
                                s->throughput, 100.0 * s->cleanHits / s->count);
            if (gGame.troubleCount > 0) {
                used += snprintf(line + used, sizeof line - used, "     Missed most:");
            }
            for (int i = 0; i < gGame.troubleCount; i++) {
                used += snprintf(line + used, sizeof line - used, "  %c x%d",
                                 gGame.troubleLetters[i], gGame.troubleMisses[i]);
            }
        }
        ui_text_centered(line, (Vector2){c.x, c.y + 72}, 20, WHITE);
    }

    Rectangle again = {c.x - 190, c.y + 120, 180, 44};
    Rectangle menu = {c.x + 10, c.y + 120, 180, 44};
    if (ui_button(again, "Play again", false)) start_round(gGame.mode);
    if (ui_button(menu, "Menu", false)) gGame.screen = SCREEN_MENU;
    ui_text_centered("Esc for menu", (Vector2){c.x, c.y + 190}, 16, WHITE);
}

/* ======================================================================== */
/* 8. The settings panel                                                     */
/* ======================================================================== */

/* One slider: a label, the setting it changes, and its range. */
typedef struct {
    const char *label;
    float *value; /* points at the field of gSet this slider controls */
    float min, max, step;
} Slider;

enum { SLIDER_ROUND, SLIDER_SCATTER, SLIDER_FREQUENCY, SLIDER_FOCUS, SLIDER_COUNT };

static Slider gSliders[SLIDER_COUNT] = {
    {"Round length", &gSet.roundSeconds, 10, 180, 5},
    {"Random positions after", &gSet.scatterAfter, 0, SCATTER_NEVER, 1},
    {"Letter frequency", &gSet.frequency, MIN_LEVEL, 15, 1}, /* max set per monitor */
    {"Focus on missed letters", &gSet.focusPercent, 0, 100, 5},
};
static int gDragging = -1; /* the slider being dragged, or -1 for none */

/* The value shown at the right of each slider. */
static const char *slider_value_text(int i) {
    switch (i) {
    case SLIDER_ROUND:
        return TextFormat("%.0f s", gSet.roundSeconds);
    case SLIDER_SCATTER:
        return gSet.scatterAfter >= SCATTER_NEVER
                   ? "Never"
                   : TextFormat("%.0f keys", gSet.scatterAfter);
    case SLIDER_FREQUENCY:
        return TextFormat("%.0f / %d", gSet.frequency, top_level());
    default:
        return TextFormat("%.0f%%", gSet.focusPercent);
    }
}

/* The top of the frequency slider depends on the monitor: keep it current,
 * and pull the setting back if it's now above the top. */
static void fit_frequency_to_monitor(void) {
    float top = (float)top_level();
    gSliders[SLIDER_FREQUENCY].max = top;
    if (gSet.frequency > top) gSet.frequency = top;
}

/* Layout of the panel, measured down from its top edge. */
#define PANEL_WIDTH 420.0f
#define PRESETS_Y 66.0f
#define SLIDERS_Y 124.0f
#define SLIDER_ROW 56.0f
#define CHECKBOX_ROW 48.0f

static Rectangle settings_area(void) {
    float height = SLIDERS_Y + SLIDER_COUNT * SLIDER_ROW + CHECKBOX_ROW + 80.0f;
    return (Rectangle){ui_width() - PANEL_WIDTH - 20, 70, PANEL_WIDTH, height};
}

/* Draws the panel and reacts to the mouse, all in one pass. */
static void settings_panel(void) {
    Rectangle panel = settings_area();
    float x = panel.x + 24, width = panel.width - 48;

    DrawRectangleRec(panel, BLACK);
    DrawRectangleLinesEx(panel, 1.0f, WHITE);
    ui_text("Settings", x, panel.y + 22, 24, WHITE, ALIGN_LEFT);
    if (gGame.screen == SCREEN_PLAYING || gGame.screen == SCREEN_COUNTDOWN) {
        ui_text("(paused)", x + width, panel.y + 26, 18, WHITE, ALIGN_RIGHT);
    }

    /* Easy / Medium / Hard. The one matching the settings is filled in. */
    float presetWidth = (width - 20) / PRESET_COUNT;
    for (int i = 0; i < PRESET_COUNT; i++) {
        Rectangle button = {x + i * (presetWidth + 10), panel.y + PRESETS_Y,
                            presetWidth, 36};
        if (ui_button(button, kPresets[i].name, i == current_preset())) apply_preset(i);
    }

    /* The sliders. */
    for (int i = 0; i < SLIDER_COUNT; i++) {
        Slider *s = &gSliders[i];
        float y = panel.y + SLIDERS_Y + i * SLIDER_ROW;
        Rectangle track = {x, y + 32, width, 2};
        Rectangle grabArea = {x - 12, y + 16, width + 24, 34}; /* easier to hit */

        if (ui_click_in(grabArea)) gDragging = i;
        if (gDragging == i) {
            /* Turn the mouse's position along the track into a value,
             * rounded to the slider's step. */
            float along = (ui_mouse().x - track.x) / track.width; /* 0 to 1 */
            if (along < 0) along = 0;
            if (along > 1) along = 1;
            float value = s->min + along * (s->max - s->min);
            *s->value = roundf(value / s->step) * s->step;
            if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) gDragging = -1; /* let go */
        }

        float filled = (*s->value - s->min) / (s->max - s->min); /* 0 to 1 */
        ui_text(s->label, x, y + 4, 18, WHITE, ALIGN_LEFT);
        ui_text(slider_value_text(i), x + width, y + 4, 18, WHITE, ALIGN_RIGHT);
        DrawRectangleRec(track, GRAY);
        DrawRectangleRec((Rectangle){track.x, track.y, track.width * filled, 2}, WHITE);
        DrawCircleV((Vector2){track.x + track.width * filled, track.y + 1},
                    gDragging == i ? 9.0f : 7.0f, WHITE);
    }

    /* The "Flash letter" checkbox. */
    float checkY = panel.y + SLIDERS_Y + SLIDER_COUNT * SLIDER_ROW + 4;
    if (ui_click_in((Rectangle){x, checkY, width, 28})) {
        gSet.flashLetter = !gSet.flashLetter; /* "!" flips true and false */
    }
    Rectangle box = {x, checkY + 4, 20, 20};
    DrawRectangleLinesEx(box, 1.5f, WHITE);
    if (gSet.flashLetter) { /* draw the tick as two lines */
        DrawLineEx((Vector2){box.x + 4, box.y + 10}, (Vector2){box.x + 8, box.y + 15}, 2.5f, WHITE);
        DrawLineEx((Vector2){box.x + 8, box.y + 15}, (Vector2){box.x + 16, box.y + 5}, 2.5f, WHITE);
    }
    ui_text("Flash letter (hide it, keep the timer)", box.x + 32, checkY + 5, 18,
            WHITE, ALIGN_LEFT);

    /* Reset focus and Done. */
    float buttonY = panel.y + panel.height - 62;
    float buttonWidth = (width - 10) / 2;
    Rectangle reset = {x, buttonY, buttonWidth, 40};
    Rectangle done = {x + buttonWidth + 10, buttonY, buttonWidth, 40};

    if (ui_button(reset, "Reset focus", false)) {
        memset(gGame.stats, 0, sizeof gGame.stats); /* set every record to zero */
    }
    if (ui_over(reset)) {
        /* Hovering explains the button, in a box just under the panel. */
        Rectangle tip = {panel.x, panel.y + panel.height + 8, panel.width, 58};
        DrawRectangleRec(tip, BLACK);
        DrawRectangleLinesEx(tip, 1.0f, WHITE);
        ui_text("Forgets which letters you've been missing, so", x, tip.y + 10, 16,
                WHITE, ALIGN_LEFT);
        ui_text("every letter is equally likely to come up again.", x, tip.y + 30, 16,
                WHITE, ALIGN_LEFT);
    }
    if (ui_button(done, "Done", false)) {
        /* Done starts a fresh round straight away: Casual from the menu,
         * otherwise whichever mode you were in. */
        close_settings();
        start_round(gGame.screen == SCREEN_MENU ? MODE_CASUAL : gGame.mode);
    }
}

/* ======================================================================== */
/* 9. Keyboard input                                                         */
/* ======================================================================== */

#ifdef __EMSCRIPTEN__
/* Browser only. A browser runs the game once per screen refresh (usually
 * 60 times a second), so checking keys once per frame would round every
 * reaction time to the nearest 17 ms. Instead the browser tells us about
 * each key the instant it goes down, and we note the exact time. The next
 * frame hands those presses to the game with their true timestamps. */
typedef struct {
    char letter;
    double pageTime; /* milliseconds since the page loaded */
} WebKey;

#define WEB_KEY_MAX 64
static WebKey gWebKeys[WEB_KEY_MAX];
static int gWebKeyCount = 0;

static EM_BOOL on_web_keydown(int type, const EmscriptenKeyboardEvent *event,
                              void *user) {
    (void)type;
    (void)user;
    /* event->code names the physical key: "KeyA" ... "KeyZ". */
    bool isLetter = strlen(event->code) == 4 && strncmp(event->code, "Key", 3) == 0 &&
                    event->code[3] >= 'A' && event->code[3] <= 'Z';
    /* event->repeat is true for the repeats you get by holding a key. */
    if (isLetter && !event->repeat && gWebKeyCount < WEB_KEY_MAX) {
        gWebKeys[gWebKeyCount++] = (WebKey){event->code[3], emscripten_get_now()};
    }
    return EM_FALSE; /* don't swallow the key: raylib still gets Esc etc. */
}
#endif

/* Handles every key pressed since the last frame. */
static void read_keys(void) {
    /* raylib queues each key once when it first goes down, so holding a
     * key can't count as lots of presses. */
    int key;
    while ((key = GetKeyPressed()) != 0) {
        if (key == KEY_ESCAPE) {
            if (gSettingsOpen) {
                close_settings();
            } else if (gGame.screen != SCREEN_MENU) {
                gGame.screen = SCREEN_MENU; /* leave the round */
                gGame.live = false;
            }
        }
#ifndef __EMSCRIPTEN__
        /* Desktop: letters come from the same queue, timed to this frame.
         * At 240 frames a second that is accurate to about 4 ms. */
        else if (key >= KEY_A && key <= KEY_Z) {
            press_letter((char)('A' + (key - KEY_A)), game_clock());
        }
#endif
    }

#ifdef __EMSCRIPTEN__
    /* Browser: letters come from on_web_keydown, each with its own time.
     * Page time and raylib time started at different moments, so work out
     * the difference and convert. */
    double pageMinusRaylib = emscripten_get_now() / 1000.0 - GetTime();
    for (int i = 0; i < gWebKeyCount; i++) {
        double pressedAt = gWebKeys[i].pageTime / 1000.0 - pageMinusRaylib;
        press_letter(gWebKeys[i].letter, to_game_clock(pressedAt));
    }
    gWebKeyCount = 0;
#endif
}

/* ======================================================================== */
/* 10. One frame, and main()                                                 */
/* ======================================================================== */

/* Everything that happens in one frame: read input, move the game along,
 * draw. Desktop calls this in a loop; a browser calls it once per refresh. */
static void frame(void) {
    ui_begin_frame();
    fit_frequency_to_monitor();

    /* The gear opens and closes settings; clicking outside closes them. */
#ifdef __EMSCRIPTEN__
    /* The fullscreen button was handled in the click event; just use up
     * the click so it doesn't also close the settings panel. */
    if (gFullscreenSupported && ui_click_in(fullscreen_area())) {
    } else
#endif
    if (ui_click_in(gear_area())) {
        if (gSettingsOpen) close_settings();
        else open_settings();
    } else if (gSettingsOpen &&
               !CheckCollisionPointRec(ui_mouse(), settings_area()) &&
               ui_click_anywhere()) {
        close_settings();
    }

    read_keys();

    double now = game_clock();
    if (!gSettingsOpen) update_round(now);

    ui_begin_draw();
    ui_set_enabled(!gSettingsOpen); /* buttons under the panel don't react */
    switch (gGame.screen) {
    case SCREEN_MENU:      draw_menu();         break;
    case SCREEN_COUNTDOWN: draw_countdown(now); break;
    case SCREEN_PLAYING:   draw_playing(now);   break;
    case SCREEN_RESULTS:   draw_results();      break;
    }
    draw_sidebar();
    draw_top_bar(now);
    ui_set_enabled(true);
    draw_gear();
#ifdef __EMSCRIPTEN__
    draw_fullscreen_button();
#endif
    if (gSettingsOpen) settings_panel();
    ui_end_draw();
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT); /* smooth edges */
    InitWindow(1280, 800, "BatakType " VERSION);
    SetExitKey(KEY_NULL); /* Esc is used in-game, so it mustn't quit */
    SetRandomSeed((unsigned int)time(NULL)); /* different letters every run */
    ui_init();

#ifdef __EMSCRIPTEN__
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE,
                                    on_web_keydown);
    gFullscreenSupported = web_fullscreen_supported() != 0;
    emscripten_set_mousedown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE,
                                      on_web_fullscreen_click);
    /* Hand the browser our frame function; it calls it once per refresh. */
    emscripten_set_main_loop(frame, 0, 1);
#else
    SetWindowMinSize(960, 640);
    SetTargetFPS(240); /* more frames = finer key timing (about 4 ms) */
    while (!WindowShouldClose()) {
        frame();
    }
#endif

    ui_shutdown();
    CloseWindow();
    return 0;
}
