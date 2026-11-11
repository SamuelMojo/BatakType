/* ui.c - sharp text, buttons, the mouse and the screen (see ui.h).
 *
 * Reading tips:
 *   - Variables starting with "s" (sScale, sClicked...) are this file's
 *     private memory: "static" at file level means only this file sees them.
 *   - Code between "#ifdef __EMSCRIPTEN__" and "#endif" is only compiled
 *     for the browser version. "#ifndef" is the opposite: desktop only.
 */
#include "ui.h"

#include "font_data.h" /* the built-in font, as a big array of bytes */

#include <math.h>
#include <stddef.h> /* NULL */

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

/* ------------------------------------------------------------------------ */
/* This file's memory                                                        */
/* ------------------------------------------------------------------------ */

/* Real screen pixels per layout pixel: 1 on an ordinary screen, 2 on a
 * Retina screen in the browser. Everything is drawn this many times bigger
 * so it stays sharp instead of being stretched. */
static float sScale = 1.0f;

static bool sClicked = false;    /* a click happened and nobody has used it */
static bool sEnabled = true;     /* see ui_set_enabled */
static bool sHandCursor = false; /* something clickable is under the mouse */
static double sRefresh = 1.0 / 60.0; /* see ui_refresh_seconds */

/* ------------------------------------------------------------------------ */
/* Fonts                                                                     */
/* ------------------------------------------------------------------------ */

/* The font file we draw with. It starts as the built-in "BatakSans" (same
 * letter shapes and widths as Arial). On desktop, find_font swaps in the
 * real Arial if the computer has it. A browser can't read system fonts, so
 * the web version always uses BatakSans. */
static const unsigned char *sFontFile = kBatakSansTTF;
static int sFontFileSize = (int)sizeof kBatakSansTTF;
static unsigned char *sArial = NULL; /* Arial's bytes, if we loaded it */

static void find_font(void) {
#ifndef __EMSCRIPTEN__
    const char *places[] = {
        "/System/Library/Fonts/Supplemental/Arial.ttf", /* macOS */
        "/Library/Fonts/Arial.ttf",
        "C:/Windows/Fonts/arial.ttf", /* Windows */
        "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf", /* Linux */
        "/usr/share/fonts/truetype/msttcorefonts/arial.ttf",
    };
    int placeCount = (int)(sizeof places / sizeof places[0]);
    for (int i = 0; i < placeCount; i++) {
        if (!FileExists(places[i])) continue;
        int size = 0;
        unsigned char *bytes = LoadFileData(places[i], &size);
        if (bytes != NULL && size > 0) {
            sArial = bytes;
            sFontFile = bytes;
            sFontFileSize = size;
            return;
        }
        UnloadFileData(bytes);
    }
#endif
}

/* A font is turned into a picture of all its letters at one exact size.
 * If we stretched one picture to every size, text would blur, so each size
 * we draw gets its own picture, made the first time it's needed and kept. */
#define FONT_CACHE_MAX 24
typedef struct {
    int pixels; /* letter height in real screen pixels */
    Font font;
} CachedFont;
static CachedFont sFonts[FONT_CACHE_MAX];
static int sFontCount = 0;

static void unload_fonts(void) {
    for (int i = 0; i < sFontCount; i++) UnloadFont(sFonts[i].font);
    sFontCount = 0;
}

static Font font_for(float size) {
    int pixels = (int)lroundf(size * sScale);
    if (pixels < 1) pixels = 1;

    for (int i = 0; i < sFontCount; i++) {
        if (sFonts[i].pixels == pixels) return sFonts[i].font; /* already made */
    }
    if (sFontCount == FONT_CACHE_MAX) unload_fonts(); /* full: start again */

    /* Only the big letter and the countdown are drawn huge, so huge sizes
     * get just A-Z and 0-9. That keeps their pictures small. */
    int bigSet[36];
    int *characters = NULL; /* NULL means "the usual full set" */
    int characterCount = 0;
    if (size >= 100.0f) {
        for (int i = 0; i < 26; i++) bigSet[characterCount++] = 'A' + i;
        for (int i = 0; i < 10; i++) bigSet[characterCount++] = '0' + i;
        characters = bigSet;
    }

    Font font = LoadFontFromMemory(".ttf", sFontFile, sFontFileSize, pixels,
                                   characters, characterCount);
    if (font.texture.id == 0) return GetFontDefault(); /* loading failed */
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    sFonts[sFontCount++] = (CachedFont){pixels, font};
    return font;
}

/* Moves a position onto a whole screen pixel, so text isn't smeared
 * across two pixels. */
static float snap(float position) { return roundf(position * sScale) / sScale; }

/* ------------------------------------------------------------------------ */
/* The screen                                                                */
/* ------------------------------------------------------------------------ */

float ui_width(void) { return (float)GetScreenWidth() / sScale; }
float ui_height(void) { return (float)GetScreenHeight() / sScale; }
double ui_refresh_seconds(void) { return sRefresh; }

#ifdef __EMSCRIPTEN__
/* Browser only: keep the canvas covering the page, with one canvas pixel
 * for every real screen pixel (the page's CSS keeps its on-screen size). */
static void fit_canvas_to_page(void) {
    double ratio = emscripten_get_device_pixel_ratio(); /* 2 on Retina */
    sScale = (float)((ratio >= 1.0 && ratio <= 4.0) ? ratio : 1.0);

    double cssWidth = 0, cssHeight = 0;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    int width = (int)lround(cssWidth * sScale);
    int height = (int)lround(cssHeight * sScale);
    if (width >= 1 && height >= 1 &&
        (width != GetScreenWidth() || height != GetScreenHeight())) {
        SetWindowSize(width, height);
    }
}

/* Browser only: a quick trackpad tap can go down and up between two
 * frames, and raylib (which checks once per frame) would miss it. So we
 * are told about each click the moment it happens and remember it. */
static bool sWebClick = false;
static EM_BOOL on_web_mousedown(int type, const EmscriptenMouseEvent *event,
                                void *user) {
    (void)type; /* "(void)x" just says "yes, I know I'm not using x" */
    (void)user;
    if (event->button == 0) sWebClick = true; /* 0 = left button */
    return EM_FALSE;                          /* let raylib see it too */
}
#endif

/* Works out the monitor's refresh time for ui_refresh_seconds. */
static void measure_refresh(void) {
#ifdef __EMSCRIPTEN__
    /* A browser won't say what the refresh rate is, but it runs the game
     * exactly once per refresh. So time a batch of frames, take the middle
     * value (which ignores the odd slow frame), and match it to the
     * nearest real-world refresh rate. */
    enum { SAMPLES = 31 };
    static double samples[SAMPLES];
    static int count = 0;
    static double lastFrame = 0.0;

    double now = GetTime();
    double frameTime = now - lastFrame;
    lastFrame = now;
    if (frameTime <= 0.0 || frameTime > 0.1) return; /* tab was hidden etc. */

    /* Keep the samples sorted as they arrive (an "insertion sort"). */
    int slot = count++;
    while (slot > 0 && samples[slot - 1] > frameTime) {
        samples[slot] = samples[slot - 1];
        slot--;
    }
    samples[slot] = frameTime;
    if (count < SAMPLES) return; /* batch not full yet */
    count = 0;

    double measuredHz = 1.0 / samples[SAMPLES / 2]; /* the middle sample */
    const int commonHz[] = {60, 75, 90, 100, 120, 144, 165, 240};
    int nearest = commonHz[0];
    for (int i = 1; i < (int)(sizeof commonHz / sizeof commonHz[0]); i++) {
        if (fabs(measuredHz - commonHz[i]) < fabs(measuredHz - nearest)) {
            nearest = commonHz[i];
        }
    }
    sRefresh = 1.0 / nearest;
#else
    /* On desktop the system knows. Ask about once a second, in case the
     * window has been dragged to a different monitor. */
    static double nextCheck = 0.0;
    if (GetTime() < nextCheck) return;
    nextCheck = GetTime() + 1.0;

    int hz = GetMonitorRefreshRate(GetCurrentMonitor());
    if (hz >= 30) sRefresh = 1.0 / hz;
#endif
}

/* ------------------------------------------------------------------------ */
/* Start-up, shut-down and the frame                                         */
/* ------------------------------------------------------------------------ */

void ui_init(void) {
    find_font();
#ifdef __EMSCRIPTEN__
    emscripten_set_mousedown_callback("#canvas", NULL, EM_TRUE, on_web_mousedown);
#endif
}

void ui_shutdown(void) {
    unload_fonts();
    if (sArial != NULL) UnloadFileData(sArial);
}

void ui_begin_frame(void) {
#ifdef __EMSCRIPTEN__
    fit_canvas_to_page();
    sClicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || sWebClick;
    sWebClick = false;
#else
    sClicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
#endif
    measure_refresh();
    sEnabled = true;
    sHandCursor = false;
}

void ui_begin_draw(void) {
    BeginDrawing();
    ClearBackground(BLACK);
    /* A "camera" that zooms everything by sScale: we draw in layout pixels
     * and it turns them into real screen pixels. */
    Camera2D camera = {{0, 0}, {0, 0}, 0.0f, sScale};
    BeginMode2D(camera);
}

void ui_end_draw(void) {
    EndMode2D();
    EndDrawing();
    SetMouseCursor(sHandCursor ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
}

/* ------------------------------------------------------------------------ */
/* The mouse                                                                 */
/* ------------------------------------------------------------------------ */

Vector2 ui_mouse(void) {
    Vector2 real = GetMousePosition(); /* in real pixels */
    return (Vector2){real.x / sScale, real.y / sScale};
}

void ui_set_enabled(bool enabled) { sEnabled = enabled; }

bool ui_over(Rectangle area) {
    if (!sEnabled || !CheckCollisionPointRec(ui_mouse(), area)) return false;
    sHandCursor = true;
    return true;
}

bool ui_click_in(Rectangle area) {
    if (!ui_over(area) || !sClicked) return false;
    sClicked = false; /* used up */
    return true;
}

bool ui_click_anywhere(void) {
    if (!sClicked) return false;
    sClicked = false;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Drawing                                                                   */
/* ------------------------------------------------------------------------ */

void ui_text(const char *words, float x, float y, float size, Color color,
             Align align) {
    Font font = font_for(size);
    float width = MeasureTextEx(font, words, size, 0).x;
    if (align == ALIGN_CENTER) x -= width * 0.5f;
    if (align == ALIGN_RIGHT) x -= width;
    DrawTextEx(font, words, (Vector2){snap(x), snap(y)}, size, 0, color);
}

void ui_text_centered(const char *words, Vector2 centre, float size, Color color) {
    Font font = font_for(size);
    Vector2 box = MeasureTextEx(font, words, size, 0); /* width and height */
    Vector2 topLeft = {snap(centre.x - box.x * 0.5f), snap(centre.y - box.y * 0.5f)};
    DrawTextEx(font, words, topLeft, size, 0, color);
}

bool ui_button(Rectangle area, const char *label, bool selected) {
    bool hovered = ui_over(area);
    bool filled = hovered || selected;
    if (filled) {
        DrawRectangleRec(area, WHITE);
    } else {
        DrawRectangleLinesEx(area, 1.0f, WHITE);
    }
    Vector2 middle = {area.x + area.width / 2, area.y + area.height / 2};
    ui_text_centered(label, middle, 18, filled ? BLACK : WHITE);
    return ui_click_in(area);
}
