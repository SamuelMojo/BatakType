/* ui.h - a small toolkit for putting sharp text and buttons on screen.
 *
 * Nothing in here knows about BatakType's rules. It answers questions like
 * "how big is the window?", "where is the mouse?", "was this button
 * clicked?" and "how fast does this monitor refresh?", and it draws text
 * that stays crisp on high-resolution (Retina) screens.
 *
 * Everything is measured in "layout pixels": the sizes you'd expect on an
 * ordinary screen. On a Retina screen each layout pixel covers 2x2 real
 * pixels, and this module does that scaling for you.
 *
 * How a frame uses it:
 *     ui_begin_frame();   read the mouse, measure the screen
 *     ...game logic...
 *     ui_begin_draw();    start drawing (clears to black)
 *     ...ui_text / ui_button / raylib Draw calls...
 *     ui_end_draw();      show the finished frame
 */
#ifndef BATAK_UI_H
#define BATAK_UI_H

#include "raylib.h"

#include <stdbool.h>

/* Where text sits relative to the x position you give it. */
typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } Align;

/* ---- Start-up and shut-down --------------------------------------------- */
void ui_init(void);     /* call once, after raylib's InitWindow */
void ui_shutdown(void); /* call once, before raylib's CloseWindow */

/* ---- Once per frame ----------------------------------------------------- */
void ui_begin_frame(void);
void ui_begin_draw(void);
void ui_end_draw(void);

/* ---- The screen --------------------------------------------------------- */
float ui_width(void);  /* window width in layout pixels */
float ui_height(void); /* window height in layout pixels */

/* How long the monitor takes to draw one frame, in seconds: 1/60 at 60 Hz,
 * 1/120 at 120 Hz. Nothing can be on screen for less time than this. */
double ui_refresh_seconds(void);

/* ---- The mouse ---------------------------------------------------------- */
Vector2 ui_mouse(void); /* mouse position in layout pixels */

/* Is the mouse over this area? Also turns the cursor into a pointing hand
 * for this frame, to show the area can be clicked. */
bool ui_over(Rectangle area);

/* Was this area clicked this frame? A click is "used up" by the first
 * thing that claims it, so one click can never press two things. */
bool ui_click_in(Rectangle area);

/* Was there a click that nothing has claimed yet? Claims it. */
bool ui_click_anywhere(void);

/* Turn mouse interaction off (and back on) for whatever is drawn next.
 * Used to stop buttons underneath an open panel from reacting. */
void ui_set_enabled(bool enabled);

/* ---- Drawing ------------------------------------------------------------ */

/* Text with its top at y. x is its left edge, centre or right edge,
 * depending on "align". "size" is the height of the font. */
void ui_text(const char *words, float x, float y, float size, Color color,
             Align align);

/* Text centred on a point, both across and down. */
void ui_text_centered(const char *words, Vector2 centre, float size, Color color);

/* An outlined button; filled in white when hovered or "selected".
 * Returns true on the frame it is clicked. */
bool ui_button(Rectangle area, const char *label, bool selected);

#endif
