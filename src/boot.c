/* psp5 – made by Mille Weber */
/* Startskärm i stil med en modern konsols uppstart: ljuspartiklar virvlar in mot
 * mitten, möts i en blixt och lämnar kvar psp5-loggan, som ett ljusskimmer sveper
 * över innan allt tonar ut. Loggan ritas med egna linjer – inga bilder eller
 * andras logotyper. Valfri knapp hoppar över. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <math.h>
#include "boot.h"
#include "gfx.h"
#include "sound.h"
#include "testrun.h"

#define PI 3.14159265f
#define PARTICLES 72
#define TRAIL 4

#define T_CONVERGE 2.25f /* partiklarna möts, loggan tänds */
#define T_SWEEP    2.75f /* ljusskimret börjar */
#define T_FADE     4.35f /* uttoning börjar */
#define T_END      5.05f

/* loggans mått (pixlar vid skala 1): x-höjd 34, p:s nedstapel till 50 */
#define LOGO_W 137.0f
#define LOGO_H 34.0f
#define STROKE 6.5f
#define MAX_PTS 48

typedef struct {
    float a0, r0, delay, speed, size, hue;
} Particle;

static Particle s_p[PARTICLES];

typedef struct {
    float xy[MAX_PTS * 2];
    int n;
} Path;

static Path s_paths[8];
static int s_path_count;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

static unsigned s_seed = 0x5eed5;
static float rnd(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return (s_seed >> 8) / 16777216.0f;
}

/* --- loggan: bokstäverna som linjer --- */

static Path *new_path(void)
{
    Path *p = &s_paths[s_path_count++];
    p->n = 0;
    return p;
}

static void add_pt(Path *p, float x, float y)
{
    if (p->n < MAX_PTS) {
        p->xy[p->n * 2] = x;
        p->xy[p->n * 2 + 1] = y;
        p->n++;
    }
}

/* båge runt (cx, cy) från vinkel a0 till a1 (grader, y nedåt: -90 = upp) */
static void add_arc(Path *p, float cx, float cy, float r, float a0, float a1, int steps)
{
    for (int i = 0; i <= steps; i++) {
        float a = (a0 + (a1 - a0) * i / steps) * PI / 180;
        add_pt(p, cx + r * cosf(a), cy + r * sinf(a));
    }
}

static void build_p(float x)
{
    Path *stem = new_path();
    add_pt(stem, x + 3, 0);
    add_pt(stem, x + 3, 50);
    Path *bowl = new_path();
    add_arc(bowl, x + 18, 17, 15, -180, 180, 40);
}

static void build_logo(void)
{
    s_path_count = 0;
    build_p(0);
    Path *s = new_path(); /* s: två halvcirklar */
    add_arc(s, 53, 8.5f, 8.5f, -30, -270, 22);
    add_arc(s, 53, 25.5f, 8.5f, -90, 150, 22);
    build_p(70);
    Path *five = new_path(); /* 5: tak, stapel och mage */
    add_pt(five, 135, 0);
    add_pt(five, 116, 0);
    add_pt(five, 115.5f, 17);
    add_arc(five, 125, 22.5f, 11.5f, -150, 145, 30);
}

/* Ritar loggan centrerad i (cx, cy). sweep = ljusskimrets x-position (skärmkoordinat). */
static void draw_logo(float cx, float cy, float scale, float alpha, float sweep)
{
    static u32 col[MAX_PTS];
    float ox = cx - LOGO_W / 2 * scale, oy = cy - LOGO_H / 2 * scale - 4 * scale;
    for (int pass = 0; pass < 2; pass++) {
        /* ett tunt mjukt lager under kärnan ger mjukare kanter */
        float w = (pass == 0 ? STROKE + 1.5f : STROKE) * scale;
        float base = pass == 0 ? 0.4f : 1.0f;
        for (int k = 0; k < s_path_count; k++) {
            Path *p = &s_paths[k];
            float pts[MAX_PTS * 2];
            for (int i = 0; i < p->n; i++) {
                float x = ox + p->xy[i * 2] * scale, y = oy + p->xy[i * 2 + 1] * scale;
                pts[i * 2] = x;
                pts[i * 2 + 1] = y;
                float d = (x - sweep) / 26.0f;
                float hl = expf(-d * d); /* ljusare där skimret passerar */
                int r = 200 + (int)(55 * hl), g = 212 + (int)(43 * hl);
                float a = alpha * base;
                col[i] = RGBA(r, g, 255, (int)(255 * clampf(a, 0, 1)));
            }
            gfx_stroke(pts, col, p->n, w);
        }
    }
}

/* --- partiklar --- */

static void init_particles(void)
{
    for (int i = 0; i < PARTICLES; i++) {
        Particle *p = &s_p[i];
        p->a0 = rnd() * 2 * PI;
        p->r0 = 230 + rnd() * 160;
        p->delay = 0.25f + rnd() * 0.7f;
        p->speed = 1.6f + rnd() * 0.9f; /* varv under inflygningen */
        p->size = 0.8f + rnd() * 1.6f;
        p->hue = rnd();
    }
}

/* Position för partikeln vid tiden t; returnerar ljusstyrka 0–1. */
static float particle_at(const Particle *p, float t, float *x, float *y)
{
    float cx = SCREEN_W / 2, cy = SCREEN_H / 2;
    if (t < T_CONVERGE) {
        float e = clampf((t - p->delay) / (T_CONVERGE - p->delay), 0, 1);
        if (e <= 0)
            return 0;
        float k = e * e * e; /* långsamt först, snabbt in mot mitten */
        float r = p->r0 * (1 - k);
        float a = p->a0 + p->speed * k * 2 * PI * 0.35f;
        *x = cx + r * cosf(a);
        *y = cy + r * sinf(a) * 0.62f;
        return smooth(e * 3) * (0.5f + 0.5f * k);
    }
    /* efter blixten: driver långsamt utåt och slocknar */
    float e = clampf((t - T_CONVERGE) / 1.8f, 0, 1);
    float a = p->a0 + p->speed * 2 * PI * 0.35f + e * 0.6f;
    float r = 18 + e * (60 + p->r0 * 0.25f);
    *x = cx + r * cosf(a);
    *y = cy + r * sinf(a) * 0.62f;
    return (1 - e) * 0.6f;
}

static void draw_particles(float t, float alpha)
{
    gfx_blend_add(1);
    for (int i = 0; i < PARTICLES; i++) {
        Particle *p = &s_p[i];
        int cb = 255, cg = 200 + (int)(55 * p->hue), cr = 150 + (int)(90 * p->hue);
        for (int k = TRAIL - 1; k >= 0; k--) {
            float x, y;
            float b = particle_at(p, t - k * 0.035f, &x, &y);
            if (b <= 0)
                continue;
            float fade = 1 - k / (float)TRAIL;
            float s = p->size * (0.6f + 0.4f * fade);
            gfx_dot(x, y, s, RGBA(cr, cg, cb, (int)(255 * clampf(b * fade * alpha, 0, 1))), 0);
            if (k == 0)
                gfx_dot(x, y, s * 5, RGBA(cr, cg, cb, (int)(80 * clampf(b * alpha, 0, 1))), 1);
        }
    }
    gfx_blend_add(0);
}

/* --- bildruta --- */

static void draw_frame(float t, float out)
{
    float cx = SCREEN_W / 2, cy = SCREEN_H / 2;
    gfx_begin(RGBA(0, 0, 0, 255));

    /* djupblått ljus som växer fram bakom allt */
    float bg = smooth((t - 0.6f) / 1.8f) * out;
    gfx_gradient_v(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 4, 14, (int)(255 * bg)), RGBA(2, 10, 34, (int)(255 * bg)));
    gfx_blend_add(1);
    gfx_glow(cx - 90, cy - 30, 180, 60, 150, RGBA(30, 70, 190, (int)(120 * bg)));
    gfx_glow(-60, SCREEN_H - 40, SCREEN_W + 120, 30, 90, RGBA(20, 50, 150, (int)(60 * bg)));
    gfx_blend_add(0);

    draw_particles(t, out);

    /* blixten när partiklarna möts */
    float flash = expf(-fabsf(t - T_CONVERGE) * (t < T_CONVERGE ? 9.0f : 3.2f));
    if (flash > 0.01f) {
        gfx_blend_add(1);
        gfx_glow(cx - 40, cy - 12, 80, 24, 120 * flash + 20, RGBA(160, 200, 255, (int)(200 * flash * out)));
        gfx_glow(cx - 6, cy - 6, 12, 12, 40, RGBA(255, 255, 255, (int)(230 * flash * out)));
        gfx_blend_add(0);
    }

    /* loggan */
    float show = smooth((t - (T_CONVERGE - 0.1f)) / 0.55f);
    if (show > 0) {
        float scale = 1.0f + 0.14f * (1 - smooth((t - T_CONVERGE + 0.1f) / 1.2f));
        float sweep_t = (t - T_SWEEP) / 1.1f;
        float sweep = sweep_t < 0 ? -1000 : cx - 140 + sweep_t * 280;
        gfx_blend_add(1);
        gfx_glow(cx - 60, cy - 18, 120, 36, 60, RGBA(60, 110, 230, (int)(70 * show * out)));
        gfx_blend_add(0);
        draw_logo(cx, cy, scale, show * out, sweep);
    }
    gfx_end();
}

void boot_run(volatile int *running)
{
    build_logo();
    init_particles();

    /* vänta (högst 1,5 s) på att ljuden räknats fram, så att ljud och bild följs åt */
    for (int i = 0; i < 150 && !sound_ready(); i++) {
        gfx_begin(RGBA(0, 0, 0, 255));
        gfx_end();
    }
    sound_play(SND_BOOT);

    u64 start = sceKernelGetSystemTimeWide();
    float skip_at = -1; /* tid då en knapp trycktes */
    u32 old = ~0u;      /* knappar som hålls vid start räknas inte */
    while (*running) {
        float t = (sceKernelGetSystemTimeWide() - start) / 1000000.0f;
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        u32 b = testrun_buttons(pad.Buttons);
        if ((b & ~old) && skip_at < 0 && t > 0.3f) {
            skip_at = t;
            sound_fade_out();
        }
        old = b;

        float out = 1 - smooth((t - T_FADE) / (T_END - T_FADE));
        if (skip_at >= 0)
            out *= 1 - smooth((t - skip_at) / 0.3f);
        if (t >= T_END || (skip_at >= 0 && t - skip_at >= 0.3f))
            break;
        draw_frame(t, out);
    }
    gfx_begin(RGBA(0, 0, 0, 255));
    gfx_end();
}
