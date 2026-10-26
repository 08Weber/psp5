/* psp5 – made by 08Weber */
/* Liten 2D-renderare direkt mot GU: rektanglar, gradienter, texturer,
 * rundade former via 9-slice och text via intraFont. */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <intraFont.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "testrun.h"

#define BUF_W 512
#define FB_SIZE (BUF_W * SCREEN_H * 4)
#define SHAPE_SIZE 64

typedef struct { float u, v; u32 color; float x, y, z; } TVert;
typedef struct { u32 color; float x, y, z; } CVert;

static u32 __attribute__((aligned(64))) s_list[0x20000 / 4];
static Texture *s_disc; /* skarp cirkel (rundade hörn) */
static Texture *s_soft; /* mjuk radiell gradient (skugga/glöd) */
static Texture *s_ring; /* tunn cirkelring (markeringsram) */
static intraFont *s_font, *s_bold, *s_jpn;
static void *s_draw; /* bufferten som ritas just nu (relativ VRAM) */
/* Senast bundna textur: många former i rad (rundade rutor, prickar) använder samma, och då
 * behöver GU inte ställas om och texturcachen tömmas igen. intraFont binder sina egna. */
static const Texture *s_bound;

/* --- texturer --- */

static int pow2(int v)
{
    int p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

Texture *tex_create(int w, int h, int psm)
{
    Texture *t = calloc(1, sizeof(Texture));
    if (!t)
        return NULL;
    int bpp = psm == GU_PSM_8888 ? 4 : 2;
    t->w = w;
    t->h = h;
    t->tw = pow2(w);
    t->th = pow2(h);
    t->psm = psm;
    t->stride = (w + 7) & ~7; /* GU kräver 16-bytesjusterade rader */
    /* en extra rad så att bilinjär filtrering vid nederkanten läser giltigt minne */
    t->data = memalign(16, t->stride * (h + 1) * bpp);
    if (!t->data) {
        free(t);
        return NULL;
    }
    memset(t->data, 0, t->stride * (h + 1) * bpp);
    return t;
}

void tex_free(Texture *t)
{
    if (!t)
        return;
    if (t == s_bound)
        s_bound = NULL; /* en ny textur kan få samma adress */
    free(t->data);
    free(t);
}

void tex_flush(Texture *t)
{
    int bpp = t->psm == GU_PSM_8888 ? 4 : 2;
    sceKernelDcacheWritebackRange(t->data, t->stride * (t->h + 1) * bpp);
}

enum { SHAPE_DISC, SHAPE_SOFT, SHAPE_RING };

static Texture *make_shape(int kind)
{
    Texture *t = tex_create(SHAPE_SIZE, SHAPE_SIZE, GU_PSM_8888);
    if (!t)
        return NULL;
    u32 *px = t->data;
    float c = SHAPE_SIZE / 2.0f;
    for (int y = 0; y < SHAPE_SIZE; y++) {
        for (int x = 0; x < SHAPE_SIZE; x++) {
            float dx = x + 0.5f - c, dy = y + 0.5f - c;
            float d = sqrtf(dx * dx + dy * dy);
            float a;
            if (kind == SHAPE_SOFT) {
                float n = d / c;
                a = n >= 1.0f ? 0.0f : expf(-n * n * 4.0f) * (1.0f - n);
            } else if (kind == SHAPE_RING) {
                /* ring mellan radie c-6.5 och c-0.5, kantutjämnad på båda sidor */
                float outer = c - 0.5f - d, inner = d - (c - 6.5f);
                a = fminf(outer, inner);
                a = a < 0 ? 0 : a > 1 ? 1 : a;
            } else {
                a = c - 0.5f - d;
                a = a < 0 ? 0 : a > 1 ? 1 : a;
            }
            px[y * t->stride + x] = RGBA(255, 255, 255, (int)(a * 255.0f + 0.5f));
        }
    }
    tex_flush(t);
    return t;
}

/* --- grundläggande GU --- */

void gfx_init(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, s_list);
    s_draw = (void *)0;
    sceGuDrawBuffer(GU_PSM_8888, s_draw, BUF_W);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, (void *)FB_SIZE, BUF_W);
    sceGuOffset(2048 - SCREEN_W / 2, 2048 - SCREEN_H / 2);
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuShadeModel(GU_SMOOTH);
    sceGuFinish();
    sceGuSync(0, 0);
    /* båda bildbuffertarna svarta innan skärmen slås på – annars syns förra lägets sista
     * bild en kort stund (blinkade vid byte till USB-läge och filhanteraren) */
    for (int i = 0; i < 2; i++) {
        gfx_begin(RGBA(0, 0, 0, 255));
        gfx_end();
    }
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    s_disc = make_shape(SHAPE_DISC);
    s_soft = make_shape(SHAPE_SOFT);
    s_ring = make_shape(SHAPE_RING);
}

void gfx_term(void)
{
    tex_free(s_disc);
    tex_free(s_soft);
    tex_free(s_ring);
    s_disc = s_soft = s_ring = NULL;
    sceGuDisplay(GU_FALSE);
    sceGuTerm();
}

void gfx_begin(u32 clear_color)
{
    sceGuStart(GU_DIRECT, s_list);
    s_bound = NULL;
    sceGuClearColor(clear_color);
    sceGuClear(GU_COLOR_BUFFER_BIT);
}

void gfx_end(void)
{
    gfx_end_with(NULL);
}

void gfx_end_with(void (*after_draw)(void))
{
    sceGuFinish();
    sceGuSync(0, 0);
    if (after_draw)
        after_draw();
    testrun_frame((const void *)(0x44000000 + (u32)s_draw)); /* bara i testbyggen */
    sceDisplayWaitVblankStart();
    s_draw = sceGuSwapBuffers();
}

static void bind(const Texture *t)
{
    sceGuEnable(GU_TEXTURE_2D);
    if (t == s_bound)
        return;
    s_bound = t;
    sceGuTexMode(t->psm, 0, 0, 0);
    sceGuTexImage(0, t->tw, t->th, t->stride, t->data);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFlush();
}

#define TVERT_FMT (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)
#define CVERT_FMT (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)

void gfx_rect(float x, float y, float w, float h, u32 color)
{
    gfx_gradient_v(x, y, w, h, color, color);
}

void gfx_gradient_v(float x, float y, float w, float h, u32 top, u32 bottom)
{
    CVert *v = sceGuGetMemory(4 * sizeof(CVert));
    v[0] = (CVert){ top, x, y, 0 };
    v[1] = (CVert){ top, x + w, y, 0 };
    v[2] = (CVert){ bottom, x, y + h, 0 };
    v[3] = (CVert){ bottom, x + w, y + h, 0 };
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_TRIANGLE_STRIP, CVERT_FMT, 4, 0, v);
}

void gfx_gradient_h(float x, float y, float w, float h, u32 left, u32 right)
{
    CVert *v = sceGuGetMemory(4 * sizeof(CVert));
    v[0] = (CVert){ left, x, y, 0 };
    v[1] = (CVert){ right, x + w, y, 0 };
    v[2] = (CVert){ left, x, y + h, 0 };
    v[3] = (CVert){ right, x + w, y + h, 0 };
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_TRIANGLE_STRIP, CVERT_FMT, 4, 0, v);
}

void gfx_line(float x0, float y0, float x1, float y1, u32 color)
{
    CVert *v = sceGuGetMemory(2 * sizeof(CVert));
    v[0] = (CVert){ color, x0, y0, 0 };
    v[1] = (CVert){ color, x1, y1, 0 };
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_LINES, CVERT_FMT, 2, 0, v);
}

void gfx_tex(const Texture *t, float x, float y, float w, float h, u32 tint)
{
    if (!t || t->w < 2)
        return;
    /* Lodräta remsor om 64 texlar: en enda stor sprite (t.ex. bakgrunden över hela skärmen)
     * räcker inte texturcachen till och sänker bildfrekvensen (issue #9).
     * En halv texel in så att filtreringen aldrig läser utanför bilden. */
    int n = (t->w + 63) / 64;
    float scale = w / (t->w - 1); /* skärmpixlar per texel */
    TVert *v = sceGuGetMemory(2 * n * sizeof(TVert));
    for (int k = 0; k < n; k++) {
        float u0 = k ? k * 64 : 0.5f, u1 = k < n - 1 ? (k + 1) * 64 : t->w - 0.5f;
        v[k * 2] = (TVert){ u0, 0.5f, tint, x + (u0 - 0.5f) * scale, y, 0 };
        v[k * 2 + 1] = (TVert){ u1, t->h - 0.5f, tint, x + (u1 - 0.5f) * scale, y + h, 0 };
    }
    bind(t);
    sceGuDrawArray(GU_SPRITES, TVERT_FMT, 2 * n, 0, v);
}

/* 9-slice av en form-textur: hörnen får radien r, mitten sträcks ut. */
static void nine_slice(const Texture *t, float x, float y, float w, float h, float r, u32 color)
{
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    const float S = SHAPE_SIZE, H = SHAPE_SIZE / 2.0f;
    /* källa (u) och mål (x) för tre kolumner/rader: kant, mitt, kant */
    float su[4] = { 0, H - 0.5f, H + 0.5f, S };
    float dx[4] = { x, x + r, x + w - r, x + w };
    float dy[4] = { y, y + r, y + h - r, y + h };

    TVert *v = sceGuGetMemory(18 * sizeof(TVert));
    int n = 0;
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            v[n++] = (TVert){ su[col], su[row], color, dx[col], dy[row], 0 };
            v[n++] = (TVert){ su[col + 1], su[row + 1], color, dx[col + 1], dy[row + 1], 0 };
        }
    }
    bind(t);
    sceGuDrawArray(GU_SPRITES, TVERT_FMT, n, 0, v);
}

void gfx_round_rect(float x, float y, float w, float h, float r, u32 color)
{
    nine_slice(s_disc, x, y, w, h, r, color);
}

void gfx_glow(float x, float y, float w, float h, float spread, u32 color)
{
    /* den mjuka texturen faller av över halva radien – därför 2×spread */
    nine_slice(s_soft, x - spread, y - spread, w + spread * 2, h + spread * 2, spread * 2, color);
}

void gfx_circle(float cx, float cy, float r, u32 color)
{
    nine_slice(s_disc, cx - r, cy - r, r * 2, r * 2, r, color);
}

void gfx_ring(float x, float y, float w, float h, float r, u32 color)
{
    nine_slice(s_ring, x, y, w, h, r, color);
}

void gfx_frame(float x, float y, float w, float h, float t, u32 color)
{
    gfx_rect(x, y, w, t, color);
    gfx_rect(x, y + h - t, w, t, color);
    gfx_rect(x, y + t, t, h - t * 2, color);
    gfx_rect(x + w - t, y + t, t, h - t * 2, color);
}

void gfx_fan(float cx, float cy, const float *xy, int n, u32 color)
{
    CVert *v = sceGuGetMemory((n + 2) * sizeof(CVert));
    v[0] = (CVert){ color, cx, cy, 0 };
    for (int i = 0; i <= n; i++)
        v[i + 1] = (CVert){ color, xy[(i % n) * 2], xy[(i % n) * 2 + 1], 0 };
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_TRIANGLE_FAN, CVERT_FMT, n + 2, 0, v);
}

void gfx_button(float cx, float cy, float r, int kind, u32 plate, u32 mark)
{
    gfx_circle(cx, cy, r, plate);
    float s = r * 0.45f;
    switch (kind) {
    case BTN_CIRCLE:
        gfx_circle(cx, cy, r * 0.56f, mark);
        gfx_circle(cx, cy, r * 0.34f, plate);
        break;
    case BTN_CROSS:
        for (float o = -0.5f; o <= 0.5f; o += 0.5f) {
            gfx_line(cx - s + o, cy - s, cx + s + o, cy + s, mark);
            gfx_line(cx + s + o, cy - s, cx - s + o, cy + s, mark);
        }
        break;
    case BTN_TRIANGLE:
        for (float o = -0.4f; o <= 0.4f; o += 0.4f) {
            gfx_line(cx + o, cy - s - 0.5f, cx - s - 0.5f + o, cy + s * 0.8f, mark);
            gfx_line(cx - s - 0.5f + o, cy + s * 0.8f + o, cx + s + 0.5f, cy + s * 0.8f + o, mark);
            gfx_line(cx + s + 0.5f + o, cy + s * 0.8f, cx + o, cy - s - 0.5f, mark);
        }
        break;
    case BTN_SQUARE:
        gfx_frame(cx - s, cy - s, s * 2, s * 2, 1.2f, mark);
        break;
    }
}

/* --- text --- */

void font_init(int need_jpn)
{
    intraFontInit();
    s_font = intraFontLoad("flash0:/font/ltn0.pgf", INTRAFONT_CACHE_LARGE | INTRAFONT_CACHE_ALL);
    s_bold = intraFontLoad("flash0:/font/ltn4.pgf", INTRAFONT_CACHE_LARGE | INTRAFONT_CACHE_ALL);
    if (!s_bold)
        s_bold = s_font;
    if (need_jpn)
        s_jpn = intraFontLoad("flash0:/font/jpn0.pgf", INTRAFONT_STRING_UTF8);

    intraFont *fonts[] = { s_font, s_bold };
    for (int i = 0; i < 2; i++) {
        if (!fonts[i])
            continue;
        intraFontSetEncoding(fonts[i], INTRAFONT_STRING_UTF8);
        if (s_jpn)
            intraFontSetAltFont(fonts[i], s_jpn);
    }
}

void font_ensure_cjk(void)
{
    if (s_jpn || !s_font)
        return;
    s_jpn = intraFontLoad("flash0:/font/jpn0.pgf", INTRAFONT_STRING_UTF8);
    if (!s_jpn)
        return;
    intraFontSetAltFont(s_font, s_jpn);
    if (s_bold != s_font)
        intraFontSetAltFont(s_bold, s_jpn);
}

void font_term(void)
{
    if (s_bold && s_bold != s_font)
        intraFontUnload(s_bold);
    if (s_font)
        intraFontUnload(s_font);
    if (s_jpn)
        intraFontUnload(s_jpn);
    s_font = s_bold = s_jpn = NULL;
    intraFontShutdown();
}

static intraFont *pick(int bold)
{
    return bold ? s_bold : s_font;
}

float text_width(float size, int bold, const char *s)
{
    intraFont *f = pick(bold);
    if (!f)
        return 0;
    intraFontSetStyle(f, size, 0xFFFFFFFF, 0, 0.0f, 0);
    if (s_jpn)
        intraFontSetStyle(s_jpn, size, 0xFFFFFFFF, 0, 0.0f, 0);
    return intraFontMeasureText(f, s);
}

float text_fit(float size, float min_size, int bold, float max_w, const char *s)
{
    while (size > min_size && text_width(size, bold, s) > max_w)
        size -= 0.02f;
    return size;
}

void text_ellipsis(char *out, int out_size, const char *s, float size, float max_w)
{
    snprintf(out, out_size, "%s", s);
    if (text_width(size, 0, out) <= max_w)
        return;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", s);
    int n = strlen(buf);
    do {
        while (n > 0 && ((u8)buf[--n] & 0xC0) == 0x80) /* ett helt UTF-8-tecken */
            ;
        buf[n] = 0;
        snprintf(out, out_size, "%s...", buf);
    } while (n > 0 && text_width(size, 0, out) > max_w);
}

void text_draw(float x, float y, float size, int bold, u32 color, int align, const char *s)
{
    intraFont *f = pick(bold);
    if (!f)
        return;
    intraFontSetStyle(f, size, color, 0, 0.0f, 0);
    if (s_jpn) /* reservtypsnittet ärver inte stilen av sig självt */
        intraFontSetStyle(s_jpn, size, color, 0, 0.0f, 0);
    if (align != ALIGN_LEFT) {
        float w = intraFontMeasureText(f, s);
        x -= align == ALIGN_CENTER ? w / 2 : w;
    }
    intraFontPrint(f, x, y, s);
    s_bound = NULL; /* intraFont har bundit sin egen textur */
    /* intraFont slår på djuptestet efter sig – vi har ingen djupbuffert */
    sceGuDisable(GU_DEPTH_TEST);
}

void gfx_blend_add(int on)
{
    if (on)
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFFFF);
    else
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
}

void gfx_stroke(const float *xy, const u32 *colors, int n, float width)
{
    if (n < 2)
        return;
    CVert *v = sceGuGetMemory(n * 2 * sizeof(CVert));
    float h = width / 2;
    for (int i = 0; i < n; i++) {
        /* riktning från grannpunkterna – ger jämna fogar i kurvor */
        int a = i > 0 ? i - 1 : i, b = i < n - 1 ? i + 1 : i;
        float dx = xy[b * 2] - xy[a * 2], dy = xy[b * 2 + 1] - xy[a * 2 + 1];
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-4f)
            len = 1;
        float nx = -dy / len * h, ny = dx / len * h;
        v[i * 2] = (CVert){ colors[i], xy[i * 2] + nx, xy[i * 2 + 1] + ny, 0 };
        v[i * 2 + 1] = (CVert){ colors[i], xy[i * 2] - nx, xy[i * 2 + 1] - ny, 0 };
    }
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_TRIANGLE_STRIP, CVERT_FMT, n * 2, 0, v);
}

void gfx_dot(float cx, float cy, float r, u32 color, int soft)
{
    TVert *v = sceGuGetMemory(2 * sizeof(TVert));
    v[0] = (TVert){ 0, 0, color, cx - r, cy - r, 0 };
    v[1] = (TVert){ SHAPE_SIZE, SHAPE_SIZE, color, cx + r, cy + r, 0 };
    bind(soft ? s_soft : s_disc);
    sceGuDrawArray(GU_SPRITES, TVERT_FMT, 2, 0, v);
}

void gfx_clip(float x, float y, float w, float h)
{
    sceGuScissor((int)x, (int)y, (int)w, (int)h);
}

void gfx_clip_reset(void)
{
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
}

void gfx_quad(const float *xy, const u32 *colors)
{
    CVert *v = sceGuGetMemory(4 * sizeof(CVert));
    for (int i = 0; i < 4; i++)
        v[i] = (CVert){ colors[i], xy[i * 2], xy[i * 2 + 1], 0 };
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_TRIANGLE_STRIP, CVERT_FMT, 4, 0, v);
}
