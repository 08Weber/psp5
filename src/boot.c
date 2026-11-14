/* psp5 – made by 08Weber */
/* Startskärm i samma ordning som en PS5:s uppstart:
 *   1. PSP5-startloggan från boot-logotypen, centrerad på svart
 *   2. hälsovarningen, centrerad, över djupblå botten med glidande ljusfläckar
 *   3. små ljusa bokeh-cirklar som tonar över till "Welcome Back"-bilden
 * och därefter tonar launchern in. Allt ritas här, valfri knapp hoppar över. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <stdio.h>
#include <string.h>
#include "boot.h"
#include "gfx.h"
#include "image.h"
#include "sound.h"
#include "lang.h"
#include "testrun.h"
#include "logo.h"
#include "welcome.h"

/* tidslinje (sekunder) */
#define T_LOGO_IN    0.15f
#define T_LOGO_OUT   2.10f
#define T_WARN       2.55f /* varning + ljud börjar */
#define T_WARN_OUT   4.55f
#define T_BOKEH      2.75f
#define T_WELCOME    4.65f /* "Welcome Back"-bilden tonar in */
#define T_FADE       7.70f
#define T_END        8.25f

#define BIG_BOKEH   4
#define SMALL_BOKEH 9

typedef struct { float x, y, r, vx, vy, a; } Bokeh;

static Bokeh s_big[BIG_BOKEH], s_small[SMALL_BOKEH];
static Texture *s_welcome;

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

/* --- PSP5-loggan: förrenderad från res/BOOT_LOGO_SOURCE.png (tools/make_logo.ps1 → logo.h) --- */

Texture *logo_texture(void)
{
    static Texture *t;
    if (t)
        return t;
    t = tex_create(LOGO_W, LOGO_H, GU_PSM_5650);
    if (!t)
        return NULL;
    u16 *px = t->data;
    for (int y = 0; y < LOGO_H; y++)
        memcpy(px + y * t->stride, LOGO_PX + y * LOGO_W, LOGO_W * 2);
    tex_flush(t);
    return t;
}

void logo_draw(float cx, float cy, u32 color)
{
    Texture *t = logo_texture();
    gfx_tex_1to1(t, cx - LOGO_W / 2.0f, cy - LOGO_H / 2.0f, color);
}

/* loggans ursprungliga blå sken ingår i bilden */
static void draw_logo_glow(float cx, float cy, float a)
{
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

/* --- "Welcome Back"-bilden (tools/make_welcome.ps1 → welcome.h) --- */

static void draw_welcome(float s)
{
    if (s <= 0.01f)
        return;
    if (s_welcome) {
        gfx_tex(s_welcome, 0, 0, SCREEN_W, SCREEN_H, RGBA(255, 255, 255, (int)(255 * s)));
    } else {
        float rise = (1 - s) * 6;
        text_draw(SCREEN_W / 2, 142 + rise, 0.9f, 0, RGBA(226, 228, 236, (int)(240 * s)), ALIGN_CENTER, L(S_WELCOME));
    }
}

/* --- bokeh --- */

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
    if (!s_welcome) {
        Image img;
        if (img_decode_cover(WELCOME_JPG, WELCOME_JPG_SIZE, &img, SCREEN_H)) {
            s_welcome = img_to_texture(&img, GU_PSM_8888);
            img_free(&img);
        }
    }
}

static void draw_bokeh(float t)
{
    /* djupblå botten och stora, mjuka ljusfläckar som glider – ljuset bygger upp sig i lager */
    float big = s_welcome ? window(t, T_BOKEH, T_BOKEH + 1.3f, T_WELCOME + 0.25f, T_WELCOME + 1.35f)
                          : smooth((t - T_BOKEH) / 1.3f);
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
    /* små ljusa cirklar som tonar över till "Welcome Back" */
    float small = window(t, T_WARN_OUT - 0.7f, T_WARN_OUT - 0.1f, T_WARN_OUT + 0.5f, T_WARN_OUT + 1.3f);
    if (small > 0.01f) {
        float dt = t - (T_WARN_OUT - 0.7f);
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

static void draw_frame(float t, float out)
{
    gfx_begin(RGBA(0, 0, 0, 255));
    if (t < T_WARN) {
        draw_logo(t, window(t, T_LOGO_IN, T_LOGO_IN + 0.5f, T_LOGO_OUT, T_LOGO_OUT + 0.3f) * out);
    } else {
        float w = smooth((t - T_WELCOME) / 1.1f);
        if (!s_welcome || w < 0.99f)
            draw_bokeh(t);
        draw_welcome(w);
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
    tex_free(s_welcome);
    s_welcome = NULL;
#ifndef PSP5_TEST /* testbygget sparar ljudet som WAV efteråt (sound_dump) */
    sound_release(SND_BOOT); /* ~450 KB tillbaka (viktigt på PSP-1000) */
#endif
}
