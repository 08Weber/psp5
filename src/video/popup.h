/* psp5 – made by Mille Weber */
#ifndef POPUP_H
#define POPUP_H

#include "gfx.h"

/* Every popup of the app is drawn the same way: the screen dimmed, a square panel with a soft shadow, and around the
 * chosen entry a faint light fill with a thin frame. The options popup (cards and file manager) and the question before
 * a delete use the darker panel; the progress panel and the toast the lighter one. The look is defined here and
 * nowhere else. `a` (0..1) fades the whole popup in and out. */
#define POPUP_DARK  RGBA(22, 23, 28, 248)
#define POPUP_LIGHT RGBA(40, 41, 47, 248)
#define POPUP_TEXT      RGBA(242, 242, 246, 255) /* text in a popup */
#define POPUP_TEXT_DIM  RGBA(165, 168, 178, 255) /* secondary text: the size under a question, the name in a progress panel */
#define POPUP_DANGER    RGBA(255, 128, 118, 255) /* Delete */
#define POPUP_EDGE  5.0f  /* panel edge to the frame of a choice, on all four sides */
#define POPUP_ITEM  28.0f /* height of a choice */
#define POPUP_ASK_W 250.0f /* width of a question */

/* the screen dimmed behind a popup */
void popup_dim(float a);
/* the panel: soft shadow, then the panel in `color` (POPUP_DARK or POPUP_LIGHT) */
void popup_panel(float x, float y, float w, float h, u32 color, float a);
/* The chosen entry: faint light fill and a thin frame (x, y, w, h = the frame). The edges are rounded to whole pixels
 * (the way the hardware draws the edge of a rectangle): a frame on fractions of a pixel gets sides of different
 * thickness, 1 px on one and 2 px on another, and the frame must look the same on every side. */
void popup_choice(float x, float y, float w, float h, float a);
/* Where a label looks centred: its baseline is about 6.45 x its size below the middle of the frame around it (as in a
 * choice of the popups: the baseline 18 px below the top of a 28 px choice, text size 0.62). */
static inline float popup_label_mid(float baseline, float size)
{
    return baseline - 6.45f * size;
}
/* The frame, `h` high or one pixel more, around the middle `mid` that has its top edge closest to a whole pixel, so that
 * its middle is within 1/4 px of `mid` whatever `mid` is: returns the top in *y and the height in *hh. Text that
 * shrinks to fit gets a frame that follows it. */
static inline void popup_frame_around(float mid, float h, float *y, float *hh)
{
    float t0 = mid - h / 2, t1 = mid - (h + 1) / 2;
    float e0 = t0 - (int)(t0 + 0.5f), e1 = t1 - (int)(t1 + 0.5f);
    if (e0 < 0) e0 = -e0;
    if (e1 < 0) e1 = -e1;
    int tall = e1 + 0.001f < e0;
    *hh = tall ? h + 1 : h;
    *y = (float)(int)((tall ? t1 : t0) + 0.5f);
}
/* A question (delete? remove?) in the middle of the screen, in the dark panel: the text in `lines` (wrapped to
 * POPUP_ASK_W - 24), `line_h` apart, then `sub` (smaller and dim, may be NULL or empty: the size of what is deleted),
 * then the `n` choices with their labels and colours, the one numbered `sel` chosen. The caller dims the screen
 * (popup_dim) first. `a` fades it in. */
void popup_ask(const char (*lines)[96], int nlines, float line_h, const char *sub, const char *const *labels,
               const u32 *colors, int n, int sel, float a);
/* A message in the middle of the screen (not modal: the screen is not dimmed). */
void popup_toast(const char *msg, float a);
/* Copy / move / delete in progress: dims the screen and draws the panel with the title, the name of what is being
 * handled, the bar (`fraction` 0..1: look.h) and, at the bottom, the hint for cancelling: the symbol of
 * `back_button` (BTN_CIRCLE or BTN_CROSS) and `cancel`. */
void popup_progress(const char *title, const char *current, float fraction, int back_button, const char *cancel);

#endif
