/* psp5 – made by 08Weber */
/* Startskärm i samma ordning som en PS5:s uppstart:
 *   1. psp5-loggan (blå symbol + psp5) mitt på svart, med ett mjukt blått sken
 *   2. hälsovarningen, centrerad, över djupblå botten med glidande ljusfläckar
 *   3. små ljusa bokeh-cirklar och gyllene damm som virvlar upp
 *   4. en varm ljuskägla uppifrån vänster med svävande damm och golvsken
 *   5. "Välkommen tillbaka" i ljuset
 * och därefter tonar launchern in. Allt ritas här, valfri knapp hoppar över. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "boot.h"
#include "gfx.h"
#include "sound.h"
#include "lang.h"
#include "testrun.h"
#include "logo.h"

#define PI 3.14159265f

/* tidslinje (sekunder) */
#define T_LOGO_IN    0.15f
#define T_LOGO_OUT   2.10f
#define T_WARN       2.55f /* varning + ljud börjar */
#define T_WARN_OUT   4.55f
#define T_BOKEH      2.75f
#define T_DUST       4.55f /* dammet virvlar upp */
#define T_SPOT       5.50f /* ljuskäglan tänds */
#define T_WELCOME    6.00f /* "Välkommen tillbaka" i ljuset */
#define T_FADE       7.70f
#define T_END        8.25f

#define BIG_BOKEH   4
#define SMALL_BOKEH 9
#define DUST        110

typedef struct { float x, y, r, vx, vy, a; } Bokeh;
typedef struct { float x, y, size, warm, phase, speed, swirl; } Dust;

static Bokeh s_big[BIG_BOKEH], s_small[SMALL_BOKEH];
static Dust s_dust[DUST];

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
/* 0→1 mellan a och b, 1→0 mellan c och d */
static float window(float t, float a, float b, float c, float d)
{
    return smooth((t - a) / (b - a)) * (1 - smooth((t - c) / (d - c)));
}

static unsigned s_seed = 0x5eed5;
static float rnd(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return (s_seed >> 8) / 16777216.0f;
}

/* --- psp5-loggan: förrenderad och kantutjämnad (tools/make_logo.ps1 → logo.h) --- */

Texture *logo_texture(void)
{
    static Texture *t;
    if (t)
        return t;
    t = tex_create(LOGO_W, LOGO_H, GU_PSM_8888);
    if (!t)
        return NULL;
    u32 *px = t->data;
    for (int y = 0; y < LOGO_H; y++)
        memcpy(px + y * t->stride, LOGO_PX + y * LOGO_W, LOGO_W * 4);
    tex_flush(t);
    return t;
}

void logo_draw(float cx, float cy, u32 color)
{
    Texture *t = logo_texture();
    gfx_tex_1to1(t, cx - LOGO_W / 2.0f, cy - LOGO_H / 2.0f, color);
}

/* loggan med ett mjukt blått sken bakom symbolen (den blå rutan längst till vänster) */
static void draw_logo_glow(float cx, float cy, float a)
{
    gfx_blend_add(1);
    gfx_dot(cx - LOGO_W / 2.0f + 21, cy, 46, RGBA(40, 110, 255, (int)(70 * a)), 1);
    gfx_blend_add(0);
    logo_draw(cx, cy, RGBA(255, 255, 255, (int)(255 * a)));
}

/* mitt på svart, glider upp en aning när den tonas in – som konsolens egen startlogga */
static void draw_logo(float t, float alpha)
{
    if (alpha <= 0.01f)
        return;
    float rise = (1 - smooth((t - T_LOGO_IN) / 0.9f)) * 5;
    draw_logo_glow(SCREEN_W / 2, SCREEN_H / 2 + rise, alpha);
}

/* --- hälsovarningen: rubrik + text, centrerad --- */

static void draw_warning(float alpha)
{
    if (alpha <= 0.01f)
        return;
    char buf[400];
    snprintf(buf, sizeof(buf), "%s", L(S_HEALTH));
    int lines = 1;
    for (const char *p = buf; *p; p++)
        lines += *p == '\n';
    float y = SCREEN_H / 2 - (lines - 1) * 13 / 2.0f;
    char *line = buf;
    for (int i = 0; line; i++) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = 0;
        if (i == 0) /* rubriken: ljusare, med lite luft under */
            text_draw(SCREEN_W / 2, y - 6, text_fit(0.52f, 0.36f, 0, SCREEN_W * 0.86f, line), 0,
                      RGBA(226, 228, 234, (int)(255 * alpha)), ALIGN_CENTER, line);
        else
            text_draw(SCREEN_W / 2, y, text_fit(0.40f, 0.30f, 0, SCREEN_W * 0.86f, line), 0,
                      RGBA(160, 164, 176, (int)(235 * alpha)), ALIGN_CENTER, line);
        y += 13;
        line = nl ? nl + 1 : NULL;
    }
}

/* --- "Välkommen tillbaka" i ljuset, sist (loggan visas bara i början) --- */

static void draw_welcome(float t)
{
    float a = smooth((t - T_WELCOME) / 0.8f);
    if (a <= 0.01f)
        return;
    float rise = (1 - a) * 6;
    text_draw(SCREEN_W / 2, 142 + rise, 0.9f, 0, RGBA(226, 228, 236, (int)(240 * a)), ALIGN_CENTER, L(S_WELCOME));
}

/* --- bokeh och damm --- */

static void init_scene(void)
{
    s_seed = 0x5eed5;
    for (int i = 0; i < BIG_BOKEH; i++) {
        Bokeh *b = &s_big[i];
        b->x = SCREEN_W * (0.25f + 0.5f * rnd());
        b->y = SCREEN_H * (0.3f + 0.4f * rnd());
        b->r = 150 + rnd() * 70;
        b->vx = (rnd() - 0.5f) * 22;
        b->vy = (rnd() - 0.5f) * 10;
        b->a = 0.5f + rnd() * 0.5f;
    }
    for (int i = 0; i < SMALL_BOKEH; i++) {
        Bokeh *b = &s_small[i];
        b->x = SCREEN_W * (0.3f + 0.6f * rnd());
        b->y = SCREEN_H * (0.15f + 0.7f * rnd());
        b->r = 9 + rnd() * 16;
        b->vx = (rnd() - 0.6f) * 26;
        b->vy = (rnd() - 0.5f) * 14;
        b->a = 0.4f + rnd() * 0.6f;
    }
    for (int i = 0; i < DUST; i++) {
        Dust *d = &s_dust[i];
        d->x = SCREEN_W * (0.1f + 0.85f * rnd());
        d->y = SCREEN_H * (0.05f + 0.9f * rnd());
        d->size = 0.45f + rnd() * rnd() * 1.6f;
        d->warm = rnd();
        d->phase = rnd() * 2 * PI;
        d->speed = 0.4f + rnd();
        d->swirl = 20 + rnd() * 50;
    }
}

static void draw_bokeh(float t)
{
    /* djupblå botten och stora, mjuka ljusfläckar som glider – ljuset bygger upp sig i lager */
    float big = window(t, T_BOKEH, T_BOKEH + 1.3f, T_SPOT - 0.6f, T_SPOT + 0.5f);
    if (big > 0.01f) {
        gfx_gradient_v(0, 0, SCREEN_W, SCREEN_H, RGBA(6, 14, 32, (int)(255 * big)), RGBA(2, 4, 12, (int)(255 * big)));
        float dt = t - T_BOKEH;
        gfx_blend_add(1);
        for (int i = 0; i < BIG_BOKEH; i++) {
            Bokeh *b = &s_big[i];
            float x = b->x + b->vx * dt, y = b->y + b->vy * dt;
            gfx_dot(x, y, b->r * 1.15f, RGBA(18, 46, 96, (int)(80 * big * b->a)), 1);
            gfx_dot(x, y, b->r * 0.7f, RGBA(24, 60, 120, (int)(60 * big * b->a)), 1);
        }
        gfx_blend_add(0);
    }
    /* små ljusa cirklar strax innan dammet */
    float small = window(t, T_DUST - 0.7f, T_DUST - 0.1f, T_DUST + 0.5f, T_DUST + 1.3f);
    if (small > 0.01f) {
        float dt = t - (T_DUST - 0.7f);
        gfx_blend_add(1);
        for (int i = 0; i < SMALL_BOKEH; i++) {
            Bokeh *b = &s_small[i];
            float x = b->x + b->vx * dt, y = b->y + b->vy * dt;
            gfx_dot(x, y, b->r, RGBA(70, 96, 122, (int)(70 * small * b->a)), 0);
            gfx_dot(x, y, b->r * 1.3f, RGBA(70, 96, 122, (int)(25 * small * b->a)), 1);
        }
        gfx_blend_add(0);
    }
}

static void draw_dust(float t)
{
    float vis = smooth((t - T_DUST + 0.4f) / 0.6f);
    if (vis <= 0.01f)
        return;
    float dt = t - (T_DUST - 0.4f);
    /* först ett virvlande moln, sedan lugnt svävande i ljuset */
    float burst = window(t, T_DUST - 0.4f, T_DUST + 0.2f, T_DUST + 0.6f, T_SPOT + 0.6f);
    float cx = SCREEN_W * 0.62f, cy = SCREEN_H * 0.45f;
    gfx_blend_add(1);
    for (int i = 0; i < DUST; i++) {
        Dust *d = &s_dust[i];
        float x = d->x + sinf(d->phase + dt * d->speed * 0.6f) * 8 + dt * 4;
        float y = d->y + cosf(d->phase * 1.3f + dt * d->speed * 0.5f) * 6 + dt * 2.5f;
        /* virveln drar partiklarna i en båge kring molnets mitt */
        float a = d->phase + dt * d->speed * 1.4f;
        x = x + (cx + cosf(a) * d->swirl * 2.2f - x) * burst * 0.55f;
        y = y + (cy + sinf(a) * d->swirl - y) * burst * 0.55f;
        float twinkle = 0.65f + 0.35f * sinf(d->phase * 3 + t * (2 + d->speed * 3));
        int r = 255, g = 196 + (int)(50 * (1 - d->warm)), b = 120 + (int)(120 * (1 - d->warm));
        float al = vis * twinkle * (0.55f + 0.45f * burst);
        gfx_dot(x, y, d->size, RGBA(r, g, b, (int)(255 * clampf(al, 0, 1))), 0);
        if (d->size > 1.1f)
            gfx_dot(x, y, d->size * 4, RGBA(r, g, b, (int)(50 * clampf(al, 0, 1))), 1);
    }
    gfx_blend_add(0);
}

/* varm ljuskägla från överkanten till vänster, golvsken längst ner */
static void draw_spotlight(float t)
{
    float s = smooth((t - T_SPOT) / 1.2f);
    if (s <= 0.01f)
        return;
    gfx_gradient_v(0, 0, SCREEN_W, SCREEN_H, RGBA(24, 22, 32, (int)(255 * s)), RGBA(16, 17, 28, (int)(255 * s)));
    gfx_blend_add(1);
    const float sx = SCREEN_W * 0.24f;
    /* två mjuka strålar (bred + smal): genomskinliga kanter, ljus mittlinje, bleknar nedåt */
    for (int layer = 0; layer < 2; layer++) {
        float k = layer ? 0.6f : 1.0f, a = (layer ? 44 : 40) * s;
        float tl = sx - 16 * k, tr = sx + 20 * k, tc = sx + 2;           /* överkant */
        float bl = sx - 80 * k, br = sx + 400 * k, bc = sx + 135 * k;    /* nederkant */
        u32 top = RGBA(255, 228, 208, (int)a), bot = RGBA(200, 190, 200, (int)(a * 0.22f));
        u32 clear_t = RGBA(255, 228, 208, 0), clear_b = RGBA(200, 190, 200, 0);
        float left[8] = { tl, -2, tc, -2, bl, SCREEN_H, bc, SCREEN_H };
        u32 lc[4] = { clear_t, top, clear_b, bot };
        gfx_quad(left, lc);
        float right[8] = { tc, -2, tr, -2, bc, SCREEN_H, br, SCREEN_H };
        u32 rc[4] = { top, clear_t, bot, clear_b };
        gfx_quad(right, rc);
    }
    gfx_dot(sx, -10, 70, RGBA(255, 232, 214, (int)(150 * s)), 1); /* själva lampan */
    gfx_dot(sx, -6, 26, RGBA(255, 240, 228, (int)(160 * s)), 1);
    gfx_glow(40, SCREEN_H - 14, SCREEN_W - 80, 10, 40, RGBA(150, 150, 170, (int)(45 * s)));
    gfx_blend_add(0);
}

static void draw_frame(float t, float out)
{
    gfx_begin(RGBA(0, 0, 0, 255));
    if (t < T_WARN) {
        draw_logo(t, window(t, T_LOGO_IN, T_LOGO_IN + 0.5f, T_LOGO_OUT, T_LOGO_OUT + 0.3f) * out);
    } else {
        draw_bokeh(t);
        draw_spotlight(t);
        draw_dust(t);
        draw_welcome(t);
        draw_warning(window(t, T_WARN, T_WARN + 0.25f, T_WARN_OUT - 0.4f, T_WARN_OUT));
        if (out < 1)
            gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(255 * (1 - out))));
    }
    gfx_end();
}

void boot_run(volatile int *running)
{
    init_scene();

    u64 start = sceKernelGetSystemTimeWide();
    int sound_started = 0;
    float skip_at = -1; /* tid då en knapp trycktes */
    u32 old = ~0u;      /* knappar som hålls vid start räknas inte */
    while (*running) {
        float t = (sceKernelGetSystemTimeWide() - start) / 1000000.0f;
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        u32 b = testrun_buttons(pad.Buttons);
        if ((b & ~old) && skip_at < 0 && t > 0.3f) {
            skip_at = t;
            if (sound_started)
                sound_fade_out();
        }
        old = b;
        /* ljudet börjar med varningen (loggan visas i tystnad, som på konsolen);
         * ljuden räknas fram medan loggan visas */
        if (!sound_started && t >= T_WARN && skip_at < 0 && sound_ready()) {
            sound_play(SND_BOOT);
            sound_started = 1;
        }

        float out = 1 - smooth((t - T_FADE) / (T_END - T_FADE));
        if (skip_at >= 0)
            out *= 1 - smooth((t - skip_at) / 0.3f);
        if (t >= T_END || (skip_at >= 0 && t - skip_at >= 0.3f))
            break;
        draw_frame(t, out);
    }
    gfx_begin(RGBA(0, 0, 0, 255));
    gfx_end();
    sound_wait(400);
#ifndef PSP5_TEST /* testbygget sparar ljudet som WAV efteråt (sound_dump) */
    sound_release(SND_BOOT); /* ~450 KB tillbaka (viktigt på PSP-1000) */
#endif
}
