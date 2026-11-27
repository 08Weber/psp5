/* psp5 – made by 08Weber */
/* Unified startup screen: crisp white PSP logo over an animated cyan/blue
 * swirling dust wave on black. Any button skips smoothly. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <math.h>
#include <string.h>
#include "boot.h"
#include "gfx.h"
#include "sound.h"
#include "testrun.h"
#include "logo.h"
#include "psp_boot_logo.h"

#define T_END        4.80f
#define T_FADE       4.10f
#define T_SOUND      0.20f

#define GLOW_COUNT   16
#define DUST_COUNT   160
#define SPARK_COUNT  28

typedef struct {
    float u;      /* normalized position along wave [0..1] */
    float spread; /* perpendicular offset from wave centerline */
    float speed;  /* drift speed along wave */
    float r;      /* dot radius */
    float phase;  /* twinkle / oscillation phase */
    float amp;    /* intensity [0.3..1.0] */
} DustPt;

static DustPt s_glow[GLOW_COUNT], s_dust[DUST_COUNT], s_spark[SPARK_COUNT];
static Texture *s_logo, *s_psp_logo;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

static unsigned s_seed = 0x5eed5;
static float rnd(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return (s_seed >> 8) / 16777216.0f;
}

/* --- Scanning screen logo (used by ui_draw_scanning in ui.c) --- */

Texture *logo_texture(void)
{
    if (s_logo)
        return s_logo;
    s_logo = tex_create(LOGO_W, LOGO_H, GU_PSM_5650);
    if (!s_logo)
        return NULL;
    u16 *px = s_logo->data;
    for (int y = 0; y < LOGO_H; y++)
        memcpy(px + y * s_logo->stride, LOGO_PX + y * LOGO_W, LOGO_W * 2);
    tex_flush(s_logo);
    return s_logo;
}

void logo_draw(float cx, float cy, u32 color)
{
    Texture *t = logo_texture();
    gfx_tex_1to1(t, cx - LOGO_W / 2.0f, cy - LOGO_H / 2.0f, color);
}

void logo_free(void)
{
    tex_free(s_logo);
    s_logo = NULL;
}

/* --- Boot screen PSP wordmark (alpha-masked white texture) --- */

static Texture *psp_boot_tex(void)
{
    if (s_psp_logo)
        return s_psp_logo;
    s_psp_logo = tex_create(PSP_LOGO_W, PSP_LOGO_H, GU_PSM_8888);
    if (!s_psp_logo)
        return NULL;
    u32 *px = s_psp_logo->data;
    for (int y = 0; y < PSP_LOGO_H; y++)
        for (int x = 0; x < PSP_LOGO_W; x++)
            px[y * s_psp_logo->stride + x] = RGBA(255, 255, 255, PSP_LOGO_ALPHA[y * PSP_LOGO_W + x]);
    tex_flush(s_psp_logo);
    return s_psp_logo;
}

/* S-curve wave centerline matching the startup artwork (cyan left -> cobalt right). */
static float wave_y(float u, float t)
{
    float base = 140.0f - 28.0f * sinf(u * 5.2f + 0.35f) * (0.45f + 0.55f * sinf(u * 3.14159265f));
    float ripple = 5.5f * sinf(u * 6.5f - t * 1.15f) + 3.0f * cosf(u * 10.0f + t * 0.7f);
    return base + ripple;
}

static void wave_color(float u, int *r, int *g, int *b)
{
    /* Left: luminous cyan (24, 175, 238) -> Right: deep royal blue (36, 108, 252) */
    float k = clampf(u, 0.0f, 1.0f);
    *r = (int)(24.0f + 14.0f * k);
    *g = (int)(178.0f - 78.0f * k);
    *b = (int)(235.0f + 20.0f * k);
}

static void init_scene(void)
{
    s_seed = 0x5eed5;
    for (int i = 0; i < GLOW_COUNT; i++) {
        DustPt *p = &s_glow[i];
        p->u = (float)i / (float)(GLOW_COUNT - 1);
        p->spread = (rnd() - 0.5f) * 14.0f;
        p->speed = 0.015f + rnd() * 0.015f;
        p->r = 32.0f + rnd() * 26.0f;
        p->phase = rnd() * 6.2831853f;
        p->amp = 0.55f + rnd() * 0.45f;
    }
    for (int i = 0; i < DUST_COUNT; i++) {
        DustPt *p = &s_dust[i];
        /* Concentrate particles toward the left cyan crest and right blue sweep */
        p->u = rnd();
        float g1 = rnd() + rnd() + rnd() - 1.5f; /* approx normal [-1.5..1.5] */
        p->spread = g1 * (12.0f + 16.0f * rnd());
        p->speed = 0.025f + rnd() * 0.045f;
        p->r = 0.85f + rnd() * 1.25f;
        p->phase = rnd() * 6.2831853f;
        p->amp = 0.35f + rnd() * 0.65f;
    }
    for (int i = 0; i < SPARK_COUNT; i++) {
        DustPt *p = &s_spark[i];
        p->u = 0.04f + rnd() * 0.92f;
        p->spread = (rnd() - 0.5f) * 125.0f;
        p->speed = 0.012f + rnd() * 0.022f;
        p->r = 1.6f + rnd() * 1.9f;
        p->phase = rnd() * 6.2831853f;
        p->amp = 0.45f + rnd() * 0.55f;
    }
}

static void draw_dust_wave(float t, float vis)
{
    if (vis <= 0.01f)
        return;

    /* Subtle left-to-right reveal sweep during the first 0.8s */
    float sweep = smooth(t / 0.85f);

    gfx_blend_add(1);

    /* 1. Soft atmospheric nebula glow along the wave */
    for (int i = 0; i < GLOW_COUNT; i++) {
        const DustPt *p = &s_glow[i];
        float u = fmodf(p->u + t * p->speed * 0.4f, 1.0f);
        float edge = sinf(3.14159265f * clampf(u, 0.0f, 1.0f));
        float reveal = clampf((sweep * 1.15f - u) * 4.0f, 0.0f, 1.0f);
        float x = -12.0f + u * (SCREEN_W + 24.0f);
        float y = wave_y(u, t) + p->spread + sinf(t * 0.9f + p->phase) * 3.0f;
        int r, g, b;
        wave_color(u, &r, &g, &b);
        float pulse = 0.82f + 0.18f * sinf(t * 1.4f + p->phase);
        int a = (int)(38.0f * vis * edge * reveal * p->amp * pulse);
        if (a > 0)
            gfx_dot(x, y, p->r, RGBA(r, g, b, a), 1);
    }

    /* 2. Fine swirling dust particles */
    for (int i = 0; i < DUST_COUNT; i++) {
        const DustPt *p = &s_dust[i];
        float u = fmodf(p->u + t * p->speed, 1.0f);
        float edge = sinf(3.14159265f * clampf(u, 0.0f, 1.0f));
        float reveal = clampf((sweep * 1.15f - u) * 4.0f, 0.0f, 1.0f);
        float x = -8.0f + u * (SCREEN_W + 16.0f);
        float swirl = sinf(t * 1.6f + p->phase) * 4.5f;
        float y = wave_y(u, t) + p->spread + swirl;
        int r, g, b;
        wave_color(u, &r, &g, &b);
        /* Brighter core particles near the centerline */
        float core = clampf(1.0f - fabsf(p->spread) / 28.0f, 0.25f, 1.0f);
        float twinkle = 0.65f + 0.35f * sinf(t * 3.1f + p->phase);
        int a = (int)(210.0f * vis * edge * reveal * core * p->amp * twinkle);
        if (a > 2)
            gfx_dot(x, y, p->r, RGBA(r + 30, g + 30, 255, a), p->r > 1.6f);
    }

    /* 3. Scattered floating bokeh sparks */
    for (int i = 0; i < SPARK_COUNT; i++) {
        const DustPt *p = &s_spark[i];
        float u = fmodf(p->u + t * p->speed, 1.0f);
        float edge = sinf(3.14159265f * clampf(u, 0.0f, 1.0f));
        float reveal = clampf((sweep * 1.15f - u) * 4.0f, 0.0f, 1.0f);
        float x = u * SCREEN_W;
        float y = wave_y(u, t) + p->spread - t * 4.0f + sinf(t * 0.8f + p->phase) * 3.0f;
        int r, g, b;
        wave_color(u, &r, &g, &b);
        float twinkle = 0.55f + 0.45f * sinf(t * 2.2f + p->phase);
        int a = (int)(135.0f * vis * edge * reveal * p->amp * twinkle);
        if (a > 2)
            gfx_dot(x, y, p->r, RGBA(r + 20, g + 25, b, a), 1);
    }

    gfx_blend_add(0);
}

static void draw_frame(float t, float out)
{
    gfx_begin(RGBA(0, 0, 0, 255));

    float wave_in = smooth(t / 0.70f);
    draw_dust_wave(t, wave_in * out);

    float logo_in = smooth((t - 0.25f) / 0.85f);
    float logo_a = logo_in * out;
    if (logo_a > 0.01f) {
        Texture *lt = psp_boot_tex();
        if (lt) {
            float rise = (1.0f - logo_in) * 4.0f;
            float lx = (SCREEN_W - PSP_LOGO_W) * 0.5f;
            float ly = (SCREEN_H - PSP_LOGO_H) * 0.5f + rise;
            gfx_tex_1to1(lt, lx, ly, RGBA(255, 255, 255, (int)(255.0f * logo_a)));
        }
    }

    if (out < 1.0f)
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(255.0f * (1.0f - out))));
    gfx_end();
}

void boot_run(volatile int *running, volatile int *ready)
{
    init_scene();
    psp_boot_tex();

    u64 start = sceKernelGetSystemTimeWide();
    int sound_started = 0;
    int skip_req = 0;
    float fade_start = -1.0f;
    float skip_at = -1.0f;
    u32 old = ~0u;
    while (*running) {
        float t = (sceKernelGetSystemTimeWide() - start) / 1000000.0f;
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        u32 b = testrun_buttons(pad.Buttons);
        if ((b & ~old) && !skip_req && t > 0.3f)
            skip_req = 1;
        old = b;

        if (!sound_started && t >= T_SOUND && !skip_req && sound_ready()) {
            sound_play(SND_BOOT);
            sound_started = 1;
        }

        int is_ready = !ready || *ready;
        if (is_ready) {
            if (skip_req && skip_at < 0.0f) {
                skip_at = t;
                if (sound_started)
                    sound_fade_out();
            }
            if (fade_start < 0.0f && t >= T_FADE)
                fade_start = t;
        }

        float out = 1.0f;
        if (fade_start >= 0.0f)
            out *= 1.0f - smooth((t - fade_start) / (T_END - T_FADE));
        if (skip_at >= 0.0f)
            out *= 1.0f - smooth((t - skip_at) / 0.3f);
        if ((fade_start >= 0.0f && t - fade_start >= (T_END - T_FADE)) ||
            (skip_at >= 0.0f && t - skip_at >= 0.3f))
            break;
        draw_frame(t, out);
    }
    gfx_begin(RGBA(0, 0, 0, 255));
    gfx_end();
    sound_wait(400);
    tex_free(s_psp_logo);
    s_psp_logo = NULL;
    logo_free();
#ifndef PSP5_TEST
    sound_release(SND_BOOT);
#endif
}
