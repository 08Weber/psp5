/* psp5 – made by 08Weber */
/* Färgteman. PS5 har inga egna färgteman, men konsolen säljs med skal i olika färger
 * (Midnight Black, Cosmic Red, Nova Pink, Starlight Blue, Galactic Purple, Deep Earth-
 * serien Volcanic Red/Cobalt Blue/Sterling Silver och Chroma-serien Indigo/Teal/Pearl),
 * och inställningen Utseende låter hemskärmen se ut som PS1 (grå konsol och knapparnas
 * symboler), PS2 (mörka "torn" och glödande klot), PS3 (XMB-vågen) eller PS4 (blå
 * vågor och partiklar). Temana här bygger på dem – allt ritas i kod, inga bilder. */
#include <math.h>
#include <string.h>
#include "theme.h"

#define PI 3.14159265f
#define C(r, g, b) RGBA(r, g, b, 255)

unsigned char g_theme = 0;
unsigned char g_theme_art = 1;

static const Theme THEMES[] = {
    /* code, namn, top, bottom, stop, sbottom, glow, haze, sheen, accent, card, deco, tone */
    { "ps5", "PS5", C(20, 20, 26), C(12, 12, 16), C(48, 46, 50), C(22, 23, 29),
      RGBA(215, 200, 190, 95), RGBA(130, 145, 160, 45), 0, C(238, 238, 242), C(40, 41, 48), DECO_SPOT, 20 },
    { "black", "Midnight Black", C(14, 14, 17), C(3, 3, 5), C(30, 30, 34), C(9, 9, 11),
      RGBA(150, 156, 172, 62), RGBA(90, 96, 112, 32), 0, C(206, 210, 220), C(30, 30, 35), DECO_SPOT, 10 },
    { "red", "Cosmic Red", C(74, 10, 20), C(16, 3, 7), C(96, 22, 32), C(26, 7, 11),
      RGBA(255, 128, 128, 72), RGBA(210, 60, 72, 42), 0, C(238, 76, 88), C(62, 22, 28), DECO_SPOT, 8 },
    { "pink", "Nova Pink", C(90, 24, 66), C(20, 6, 17), C(112, 40, 84), C(30, 10, 25),
      RGBA(255, 172, 214, 78), RGBA(232, 110, 172, 42), 0, C(246, 136, 190), C(70, 30, 56), DECO_SPOT, 8 },
    { "blue", "Starlight Blue", C(20, 52, 106), C(5, 13, 32), C(38, 70, 126), C(9, 19, 42),
      RGBA(172, 212, 255, 82), RGBA(92, 152, 232, 44), 0, C(116, 176, 248), C(32, 52, 86), DECO_SPOT, 8 },
    { "purple", "Galactic Purple", C(52, 26, 98), C(12, 6, 26), C(68, 40, 114), C(17, 9, 35),
      RGBA(204, 172, 255, 78), RGBA(142, 100, 222, 42), 0, C(176, 136, 244), C(50, 36, 80), DECO_SPOT, 8 },
    { "volcanic", "Volcanic Red", C(88, 28, 12), C(20, 6, 3), C(106, 42, 22), C(28, 10, 6),
      RGBA(255, 156, 96, 82), RGBA(232, 92, 42, 44), 0, C(252, 116, 64), C(70, 32, 22), DECO_SPOT, 8 },
    { "cobalt", "Cobalt Blue", C(14, 34, 122), C(3, 8, 36), C(30, 50, 142), C(7, 13, 46),
      RGBA(142, 172, 255, 82), RGBA(62, 104, 242, 44), 0, C(86, 136, 255), C(28, 40, 96), DECO_SPOT, 8 },
    { "silver", "Sterling Silver", C(78, 82, 92), C(21, 23, 27), C(98, 102, 110), C(29, 31, 35),
      RGBA(236, 242, 252, 82), RGBA(172, 182, 198, 42), 0, C(214, 220, 230), C(64, 68, 76), DECO_SPOT, 10 },
    { "indigo", "Chroma Indigo", C(36, 30, 94), C(8, 7, 26), C(50, 44, 110), C(12, 11, 34),
      RGBA(156, 144, 255, 78), RGBA(112, 92, 232, 42), RGBA(90, 210, 228, 44), C(138, 126, 252), C(40, 38, 80), DECO_SPOT, 8 },
    { "teal", "Chroma Teal", C(8, 70, 76), C(2, 17, 19), C(22, 86, 92), C(5, 23, 25),
      RGBA(146, 244, 232, 78), RGBA(42, 182, 172, 42), RGBA(126, 126, 255, 40), C(66, 214, 204), C(20, 60, 62), DECO_SPOT, 8 },
    { "pearl", "Chroma Pearl", C(72, 68, 78), C(19, 17, 23), C(94, 90, 100), C(27, 25, 31),
      RGBA(255, 242, 248, 86), RGBA(204, 192, 224, 42), RGBA(172, 222, 255, 44), C(238, 228, 242), C(64, 60, 70), DECO_SPOT, 10 },
    /* PS1–PS4, som PS5:s Utseende */
    { "ps1", "PS1", C(108, 108, 116), C(42, 42, 48), C(116, 116, 124), C(46, 46, 52),
      RGBA(244, 244, 248, 56), RGBA(176, 176, 186, 30), 0, C(224, 224, 232), C(80, 80, 88), DECO_PS1, 0 },
    { "ps2", "PS2", C(6, 10, 28), C(0, 0, 4), C(14, 20, 46), C(2, 2, 8),
      RGBA(60, 110, 255, 58), RGBA(40, 80, 210, 40), 0, C(96, 146, 255), C(18, 24, 48), DECO_PS2, 0 },
    { "ps3", "PS3", C(26, 42, 86), C(5, 9, 24), C(36, 54, 100), C(8, 13, 32),
      RGBA(190, 210, 255, 50), RGBA(120, 150, 220, 34), 0, C(156, 194, 255), C(30, 40, 66), DECO_PS3, 0 },
    { "ps4", "PS4", C(6, 50, 134), C(2, 13, 48), C(18, 62, 144), C(4, 18, 58),
      RGBA(124, 184, 255, 70), RGBA(60, 140, 255, 42), 0, C(76, 156, 255), C(20, 46, 96), DECO_PS4, 0 },
};
#define THEME_COUNT (int)(sizeof(THEMES) / sizeof(THEMES[0]))
_Static_assert(THEME_COUNT == THEME_FIRST_CLASSIC + 4, "PS1-PS4 ska komma sist");

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

int theme_count(void) { return THEME_COUNT; }
const Theme *theme_get(int i) { return &THEMES[i >= 0 && i < THEME_COUNT ? i : 0]; }
const Theme *theme_cur(void) { return theme_get(g_theme); }

int theme_from_code(const char *code)
{
    for (int i = 0; i < THEME_COUNT; i++) {
        int n = strlen(THEMES[i].code);
        if (!strncmp(code, THEMES[i].code, n) && (code[n] == 0 || code[n] == '\n' || code[n] == '\r'))
            return i;
    }
    return 0;
}

/* färgkanal k (0 = röd) och färg med ny alfa */
static int ch(u32 c, int k) { return (c >> (k * 8)) & 0xFF; }
static u32 alpha(u32 c, float a) { return COLOR_ALPHA(c, (int)(ch(c, 3) * clampf(a, 0, 1))); }
static u32 scaled(u32 c, float k, int a)
{
    return RGBA((int)(ch(c, 0) * k), (int)(ch(c, 1) * k), (int)(ch(c, 2) * k), a);
}

u32 theme_toggle_on(const Theme *t)
{
    return scaled(t->accent, 0.63f, 255);
}

/* --- mönster --- */

/* PS1: handkontrollens fyra symboler i sina klassiska färger, som en ruta på konsolen */
static void deco_ps1(float time, float a, float cx, float cy, float k)
{
    const float d = 24 * k, r = 10.5f * k, w = 2.2f * (0.5f + 0.5f * k);
    float bob = sinf(time * 0.8f) * 2;
    u32 cols[4] = { RGBA(64, 226, 160, 0), RGBA(255, 102, 102, 0), RGBA(124, 178, 232, 0), RGBA(255, 120, 232, 0) };
    float pos[4][2] = { { cx, cy - d }, { cx + d, cy }, { cx, cy + d }, { cx - d, cy } };
    for (int k = 0; k < 4; k++) {
        float x = pos[k][0], y = pos[k][1] + bob;
        u32 c = COLOR_ALPHA(cols[k], (int)(150 * a));
        u32 cc[2] = { c, c };
        gfx_glow(x - r, y - r, r * 2, r * 2, 8, COLOR_ALPHA(cols[k], (int)(28 * a)));
        if (k == 0) { /* △ */
            float tri[4][2] = { { x, y - r }, { x + r * 0.95f, y + r * 0.7f }, { x - r * 0.95f, y + r * 0.7f }, { x, y - r } };
            for (int e = 0; e < 3; e++) {
                float seg[4] = { tri[e][0], tri[e][1], tri[e + 1][0], tri[e + 1][1] };
                gfx_stroke(seg, cc, 2, w);
            }
        } else if (k == 1) { /* ○ */
            gfx_ring(x - r, y - r, r * 2, r * 2, r, c);
        } else if (k == 2) { /* ✕ */
            float s = r * 0.8f;
            float l1[4] = { x - s, y - s, x + s, y + s }, l2[4] = { x + s, y - s, x - s, y + s };
            gfx_stroke(l1, cc, 2, w);
            gfx_stroke(l2, cc, 2, w);
        } else { /* □ */
            float s = r * 0.78f;
            gfx_frame(x - s, y - s, s * 2, s * 2, w * 0.8f, c);
        }
    }
}

/* PS2: mörka glastorn som stiger och sjunker, och blå klot som driver i mörkret */
static void deco_ps2(float time, float a)
{
    gfx_blend_add(1);
    for (int i = 0; i < 5; i++) {
        float x = 240 + 220 * (0.5f + 0.5f * sinf(i * 2.1f + time * 0.07f * (1 + i * 0.3f)));
        float y = 60 + 150 * (0.5f + 0.5f * sinf(i * 1.3f + 1 + time * 0.05f * (1 + i * 0.2f)));
        float rr = 30 + i * 9;
        gfx_dot(x, y, rr, RGBA(36, 76, 220, (int)(34 * a)), 1);
        gfx_dot(x, y, rr * 0.22f, RGBA(120, 170, 255, (int)(40 * a)), 1);
    }
    for (int i = 0; i < 13; i++) {
        float x = 236 + i * 18.5f;
        float h = 28 + 80 * (0.5f + 0.5f * sinf(i * 1.7f + time * (0.25f + (i % 4) * 0.06f)));
        float wdt = 9 + (i % 3) * 2;
        gfx_gradient_v(x, SCREEN_H - h, wdt, h, RGBA(60, 110, 255, 0), RGBA(60, 110, 255, (int)(46 * a)));
        gfx_rect(x, SCREEN_H - h, wdt, 1, RGBA(150, 190, 255, (int)(60 * a)));
    }
    gfx_blend_add(0);
}

#define WAVE_PTS 33

/* Ett vågband över skärmen: brett och svagt, med en tunn ljus linje i mitten */
static void wave(float time, float base, float amp, float speed, float phase, u32 color,
                 float band_w, int band_a, int line_a, float a)
{
    float xy[WAVE_PTS * 2];
    u32 band[WAVE_PTS], line[WAVE_PTS];
    for (int i = 0; i < WAVE_PTS; i++) {
        float x = -10 + i * (500.0f / (WAVE_PTS - 1));
        float y = base + amp * sinf(x * 0.011f + time * speed + phase)
                + amp * 0.45f * sinf(x * 0.026f - time * speed * 1.4f + phase * 2);
        float edge = sinf(PI * clampf(x / SCREEN_W, 0, 1)); /* bleknar mot kanterna */
        xy[i * 2] = x;
        xy[i * 2 + 1] = y;
        band[i] = COLOR_ALPHA(color, (int)(band_a * edge * a));
        line[i] = COLOR_ALPHA(color, (int)(line_a * edge * a));
    }
    gfx_stroke(xy, band, WAVE_PTS, band_w);
    gfx_stroke(xy, line, WAVE_PTS, 1.3f);
}

/* PS3: XMB-vågen – tre tunna, ljusa band som böljar långsamt */
static void deco_ps3(float time, float a)
{
    gfx_blend_add(1);
    for (int w = 0; w < 3; w++)
        wave(time, 150 + w * 9, 20 - w * 5, 0.32f + w * 0.07f, w * 1.7f, RGBA(255, 255, 255, 0),
             30 - w * 8, 12, 110 - w * 25, a);
    gfx_blend_add(0);
}

/* PS4: breda, mjuka ljusblå vågor och små partiklar som stiger */
static void deco_ps4(float time, float a)
{
    gfx_blend_add(1);
    wave(time, 176, 26, 0.22f, 0.4f, RGBA(140, 200, 255, 0), 70, 14, 60, a);
    wave(time, 196, 18, 0.28f, 2.2f, RGBA(140, 200, 255, 0), 44, 12, 46, a);
    for (int i = 0; i < 16; i++) {
        float speed = 9 + (i % 5) * 4;
        float y = SCREEN_H + 10 - fmodf(time * speed + i * 37.0f, SCREEN_H + 30);
        float x = fmodf(i * 61.7f, SCREEN_W) + sinf(time * 0.6f + i) * 8;
        float fade = clampf((SCREEN_H + 10 - y) / 60, 0, 1) * clampf(y / 60, 0, 1);
        gfx_dot(x, y, 1.3f + (i % 3) * 0.6f, RGBA(180, 220, 255, (int)(150 * fade * a)), 0);
    }
    gfx_blend_add(0);
}

void theme_backdrop(const Theme *t, float time, const float tone[3], int settings, float a)
{
    if (settings) {
        gfx_gradient_v(0, 0, SCREEN_W, SCREEN_H, t->stop, t->sbottom);
        gfx_gradient_h(SCREEN_W / 2, 0, SCREEN_W / 2, SCREEN_H, scaled(t->sbottom, 0.8f, 0), scaled(t->sbottom, 0.8f, 150));
        /* strålkastarljus uppe till vänster och ett svagt dis nere */
        gfx_glow(70, -60, 60, 60, 120, alpha(t->glow, 0.84f));
        gfx_glow(90, -30, 20, 30, 40, RGBA(ch(t->glow, 0) + (255 - ch(t->glow, 0)) / 2, ch(t->glow, 1) + (255 - ch(t->glow, 1)) / 2,
                                           ch(t->glow, 2) + (255 - ch(t->glow, 2)) / 2, 50));
        gfx_glow(-30, 236, 280, 30, 46, alpha(t->haze, 0.93f));
    } else {
        float k = t->tone / 100.0f;
        gfx_gradient_v(0, 0, SCREEN_W, SCREEN_H,
                       RGBA((int)fminf(255, ch(t->top, 0) + tone[0] * k), (int)fminf(255, ch(t->top, 1) + tone[1] * k),
                            (int)fminf(255, ch(t->top, 2) + tone[2] * k), 255),
                       t->bottom);
        gfx_glow(30, -110, 170, 110, 150, t->glow);
        gfx_glow(-40, 250, 360, 40, 70, t->haze);
    }
    if (t->sheen)
        gfx_glow(330, 170, 160, 80, 120, alpha(t->sheen, settings ? 0.8f : 1));
    float da = a * (settings ? 0.6f : 1);
    switch (t->deco) {
    case DECO_PS1: /* i inställningarna uppe till höger, där listan inte ligger */
        if (settings)
            deco_ps1(time, da, 418, 30, 0.6f);
        else
            deco_ps1(time, da, 412, 160, 1);
        break;
    case DECO_PS2: deco_ps2(time, da); break;
    case DECO_PS3: deco_ps3(time, da); break;
    case DECO_PS4: deco_ps4(time, da); break;
    }
}

void theme_swatch(const Theme *t, float x, float y, float w, float h, float a)
{
    /* en liten "skärm" med temats toning och accentfärgen som en list – inte rund som ett reglage */
    gfx_rect(x, y, w, h, alpha(t->top, a));
    gfx_gradient_v(x, y + h * 0.35f, w, h * 0.65f, alpha(COLOR_ALPHA(t->bottom, 0), a), alpha(t->bottom, a));
    gfx_rect(x + 3, y + h - 4, w - 6, 2, alpha(t->accent, a));
    if (t->deco != DECO_SPOT) { /* klassiska konsoler: en liten våg som kännetecken */
        float l[6] = { x + 3, y + h * 0.55f, x + w * 0.5f, y + h * 0.3f, x + w - 3, y + h * 0.5f };
        u32 c[3] = { alpha(RGBA(255, 255, 255, 60), a), alpha(RGBA(255, 255, 255, 170), a), alpha(RGBA(255, 255, 255, 60), a) };
        gfx_stroke(l, c, 3, 1.1f);
    }
    gfx_frame(x, y, w, h, 1, alpha(RGBA(255, 255, 255, 70), a)); /* syns även på mörka teman */
}
