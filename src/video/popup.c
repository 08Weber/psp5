/* psp5 – made by Mille Weber */
#include <math.h>
#include <stdio.h>
#include "popup.h"
#include "look.h"

#define TEXT_MAIN POPUP_TEXT
#define TEXT_DIM  POPUP_TEXT_DIM

static u32 fade(u32 c, float a)
{
    a = a < 0 ? 0 : a > 1 ? 1 : a;
    return COLOR_ALPHA(c, (int)(((c >> 24) & 0xFF) * a));
}

void popup_dim(float a)
{
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(90 * a)));
}

void popup_panel(float x, float y, float w, float h, u32 color, float a)
{
    gfx_glow(x, y, w, h, 8, RGBA(0, 0, 0, (int)(120 * a)));
    gfx_rect(x, y, w, h, fade(color, a));
}

/* the pixel edge a rectangle edge at `v` is drawn at: a pixel is covered when its middle is */
static float px_edge(float v)
{
    return ceilf(v - 0.5f);
}

void popup_choice(float x, float y, float w, float h, float a)
{
    float right = px_edge(x + w), bottom = px_edge(y + h);
    x = px_edge(x);
    y = px_edge(y);
    w = right - x;
    h = bottom - y;
    gfx_rect(x, y, w, h, RGBA(255, 255, 255, (int)(16 * a)));
    gfx_frame(x, y, w, h, 1, fade(RGBA(232, 232, 236, 230), a));
}

void popup_ask(const char (*lines)[96], int nlines, float line_h, const char *sub, const char *const *labels,
               const u32 *colors, int n, int sel, float a)
{
    if (a <= 0)
        return;
    int has_sub = sub && sub[0];
    float pad = 10;
    float text_h = nlines * line_h + (has_sub ? 28.0f : 20.0f - line_h);
    float w = POPUP_ASK_W, h = pad + text_h + n * POPUP_ITEM + POPUP_EDGE;
    float x = (SCREEN_W - w) / 2, y = 70 + (1 - a) * 10;
    popup_panel(x, y, w, h, POPUP_DARK, a);
    for (int l = 0; l < nlines; l++)
        text_draw(x + 12, y + pad + 12 + l * line_h, 0.5f, 0, fade(TEXT_MAIN, a), ALIGN_LEFT, lines[l]);
    if (has_sub)
        text_draw(x + 12, y + pad + 20 + nlines * line_h, 0.56f, 0, fade(TEXT_DIM, a), ALIGN_LEFT, sub);
    for (int k = 0; k < n; k++) {
        float ry = y + pad + text_h + k * POPUP_ITEM;
        u32 col = colors[k];
        if (k == sel)
            popup_choice(x + POPUP_EDGE, ry, w - 2 * POPUP_EDGE, POPUP_ITEM, a);
        else
            col = fade(col, 0.8f);
        text_draw(x + 16, ry + 18, text_fit(0.62f, 0.45f, 0, w - 32, labels[k]), 0, fade(col, a), ALIGN_LEFT, labels[k]);
    }
}

void popup_toast(const char *msg, float a)
{
    float w = 2 * ceilf((text_width(0.62f, 0, msg) + 32) / 2), x = (SCREEN_W - w) / 2, y = 132; /* (a whole, even width: the panel sits on whole pixels) */
    popup_panel(x, y, w, 28, POPUP_LIGHT, a);
    text_draw(SCREEN_W / 2, y + 18.5f, 0.62f, 0, fade(RGBA(240, 240, 245, 255), a), ALIGN_CENTER, msg);
}

void popup_progress(const char *title, const char *current, float fraction, int back_button, const char *cancel)
{
    float f = fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;
    float w = 300, h = 96, x = (SCREEN_W - w) / 2, y = 84;
    popup_dim(1);
    popup_panel(x, y, w, h, POPUP_LIGHT, 1);
    text_draw(x + 16, y + 26, 0.66f, 0, TEXT_MAIN, ALIGN_LEFT, title);
    char name[96];
    text_ellipsis(name, sizeof(name), current, 0.5f, w - 32);
    text_draw(x + 16, y + 44, 0.5f, 0, TEXT_DIM, ALIGN_LEFT, name);
    look_bar(x + 16, y + 54, w - 32, 6, f > 0 ? (w - 32) * f : 0, 1); /* as the storage bars: the left end is the bar's, the end of the progress is straight */
    char pct[8];
    snprintf(pct, sizeof(pct), "%d %%", (int)(f * 100));
    text_draw(x + w - 16, y + 26, 0.56f, 0, TEXT_DIM, ALIGN_RIGHT, pct);
    /* the cancel hint: the button's symbol and its name */
    gfx_button(x + 23, y + h - 16, 6.5f, back_button, LOOK_HINT_PLATE, LOOK_HINT_MARK);
    text_draw(x + 34, y + h - 11.5f, 0.54f, 0, TEXT_MAIN, ALIGN_LEFT, cancel);
}
