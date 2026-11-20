/* psp5 authentic – the look of the small things that are the same on every screen: the bars, the switches, the ticks, the buttons in the hints and the cards.
 * None of it depends on the theme: the theme is the background (psp5 authentic's `theme_backdrop`) and nothing else. The colours were read from the
 * maintainer's pictures of the console's own screens. Everything is drawn from the functions below (and the colours are used nowhere else), so the same thing
 * looks the same on every screen. `a` (0..1) fades it in and out. */
#ifndef LOOK_H
#define LOOK_H

#include <math.h>
#include "gfx.h"

#define LOOK_BAR_TRACK   RGBA(255, 255, 255, 38)  /* the background of a bar: a trace of white over the screen */
#define LOOK_BAR_FILL    RGBA(177, 179, 188, 255) /* the part of a bar that is done or used */
#define LOOK_SWITCH_ON   RGBA(76, 78, 86, 255)    /* a switch that is on (the knob is at the right) ... */
#define LOOK_KNOB_ON     RGBA(203, 205, 209, 255)
#define LOOK_SWITCH_OFF  RGBA(255, 255, 255, 32)  /* ... and one that is off: a darker track, and at the left a knob that is only a ring (the track shows through it) */
#define LOOK_KNOB_OFF    RGBA(142, 144, 153, 255)
#define LOOK_TICK_FILL   RGBA(255, 255, 255, 42)  /* the box of a tick (the same whether it is ticked or not): a light fill and a lighter edge; a white check mark when it is ticked */
#define LOOK_TICK_EDGE   RGBA(255, 255, 255, 31)
#define LOOK_TICK_MARK   RGBA(255, 255, 255, 255)
#define LOOK_HINT_PLATE  RGBA(191, 191, 195, 255) /* the button in a hint: a light grey plate with a dark grey symbol (nothing dark behind it) */
#define LOOK_HINT_MARK   RGBA(64, 64, 69, 255)
#define LOOK_CARD        RGBA(14, 17, 21, 255)    /* a card that has no picture (yet): near black */

#define LOOK_WHEEL       RGBA(238, 238, 242, 255) /* the dots of the loading wheel */

#define LOOK_TICK_SIZE 14 /* a tick is a rounded square of this many pixels */

static inline u32 look_fade(u32 c, float a)
{
    float k = a < 0 ? 0 : a > 1 ? 1 : a;
    return COLOR_ALPHA(c, (int)(((c >> 24) & 0xFF) * k));
}

/* The loading wheel: eight dots in a ring around (cx, cy), the light one runs round (`t` is the time in seconds). It stands for "a moment": the picture viewer and the video player show it while a picture
 * or a video is being read, in place of a text (no word to translate). */
static inline void look_wheel(float cx, float cy, float t, float a)
{
    int head = (int)(t * 9.0f) & 7;
    for (int i = 0; i < 8; i++) {
        float ang = (float)i * 0.78539816f;
        int age = (head - i + 8) & 7;
        gfx_circle(cx + 16.0f * cosf(ang), cy + 16.0f * sinf(ang), 2.6f, look_fade(COLOR_ALPHA(LOOK_WHEEL, 255 - age * 28), a));
    }
}

/* A bar `w` wide: its background, and on it the part that is done, `fill_w` wide (0 = nothing yet) in `fill`. The left end of both is round (`h` / 2) and the end of the
 * part that is done is a straight edge. */
static inline void look_bar_color(float x, float y, float w, float h, float fill_w, u32 fill, float a)
{
    gfx_round_rect(x, y, w, h, h / 2, look_fade(LOOK_BAR_TRACK, a));
    if (fill_w > 0) {
        u32 color = look_fade(fill, a);
        if (fill_w < h) {
            gfx_round_rect(x, y, fill_w, h, h / 2, color);
        } else {
            gfx_round_rect(x, y, h, h, h / 2, color);
            gfx_rect(x + h / 2, y, fill_w - h / 2, h, color);
        }
    }
}
static inline void look_bar(float x, float y, float w, float h, float fill_w, float a)
{
    look_bar_color(x, y, w, h, fill_w, LOOK_BAR_FILL, a);
}

/* A switch, 26 x 14 with its top left corner at (x, y): the knob 10 across, 2 px from the edge on every side (all on whole pixels). On: a grey track with a light knob at the
 * right. Off: a darker track with a knob at the left that is only a ring. */
static inline void look_switch(float x, float y, int on, float a)
{
    gfx_round_rect(x, y, 26, 14, 7, look_fade(on ? LOOK_SWITCH_ON : LOOK_SWITCH_OFF, a));
    if (on)
        gfx_circle(x + 19, y + 7, 5, look_fade(LOOK_KNOB_ON, a));
    else
        gfx_ring(x + 2, y + 2, 10, 10, 5, look_fade(LOOK_KNOB_OFF, a));
}

/* A tick with its middle at (cx, cy): a rounded square (not a circle) on whole pixels, with a light fill and a lighter edge, and a white check mark in it when it is ticked. */
static inline void look_tick(float cx, float cy, int ticked, float a)
{
    float x = (float)((int)(cx + 0.5f) - LOOK_TICK_SIZE / 2), y = (float)((int)(cy + 0.5f) - LOOK_TICK_SIZE / 2);
    gfx_round_rect(x, y, LOOK_TICK_SIZE, LOOK_TICK_SIZE, 3.5f, look_fade(LOOK_TICK_FILL, a));
    gfx_ring(x, y, LOOK_TICK_SIZE, LOOK_TICK_SIZE, 3.5f, look_fade(LOOK_TICK_EDGE, a));
    if (ticked) {
        float mx = x + LOOK_TICK_SIZE / 2.0f, my = y + LOOK_TICK_SIZE / 2.0f;
        float xy[6] = { mx - 3, my, mx - 1, my + 2.2f, mx + 3, my - 2.4f };
        u32 c = look_fade(LOOK_TICK_MARK, a), col[3] = { c, c, c };
        gfx_stroke(xy, col, 3, 1.5f);
    }
}

/* How many of the two sound waves of the volume symbol (gfx_volume_icon) the volume bar lights for a level 0 to 1: none up to 0.12, one up to 0.5, both above; -1 (a cross) when the sound is off. */
static inline int look_volume_waves(float level, int muted)
{
    return muted ? -1 : level > 0.5f ? 2 : level > 0.12f ? 1 : 0;
}

#endif
