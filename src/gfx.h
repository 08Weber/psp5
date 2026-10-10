/* psp5 – made by Mille Weber */
#ifndef GFX_H
#define GFX_H

#include <psptypes.h>
#include <pspgu.h>

#define SCREEN_W 480
#define SCREEN_H 272

/* GU-färger är ABGR */
#define RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))
#define COLOR_ALPHA(c, a) (((c) & 0x00FFFFFF) | ((u32)(a) << 24))

typedef struct {
    int w, h;     /* bildens storlek */
    int tw, th;   /* tvåpotens ≥ w/h (för GU) */
    int stride;   /* pixlar per rad i minnet */
    int psm;      /* GU_PSM_8888 eller GU_PSM_5650 */
    void *data;
} Texture;

enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

void gfx_init(void);
void gfx_term(void);
void gfx_begin(u32 clear_color);
void gfx_end(void); /* väntar på vblank och byter buffert */

void gfx_rect(float x, float y, float w, float h, u32 color);
void gfx_gradient_v(float x, float y, float w, float h, u32 top, u32 bottom);
void gfx_gradient_h(float x, float y, float w, float h, u32 left, u32 right);
void gfx_line(float x0, float y0, float x1, float y1, u32 color);
void gfx_tex(const Texture *t, float x, float y, float w, float h, u32 tint);
/* Rundad rektangel med kantutjämning */
void gfx_round_rect(float x, float y, float w, float h, float r, u32 color);
/* Mjuk skugga/glöd: rektangeln utökas med `spread` åt alla håll */
void gfx_glow(float x, float y, float w, float h, float spread, u32 color);
void gfx_circle(float cx, float cy, float r, u32 color);
/* Tunn rundad ram (ca 2 px vid r = 10) */
void gfx_ring(float x, float y, float w, float h, float r, u32 color);
/* Rak ram med tjockleken t */
void gfx_frame(float x, float y, float w, float h, float t, u32 color);
/* Fylld polygon runt (cx, cy): n punkter i xy[2*n] */
void gfx_fan(float cx, float cy, const float *xy, int n, u32 color);
/* Kugghjul med radien r */
void gfx_gear(float cx, float cy, float r, u32 color, u32 hole);

/* Additiv blandning (ljus som lyser upp det som redan ritats) på/av */
void gfx_blend_add(int on);
/* Tjock linje genom n punkter (xy[2*n]) med en färg per punkt */
/* Prick med radien r i en enda sprite – billig för många partiklar. soft = mjuk kant */
void gfx_dot(float cx, float cy, float r, u32 color, int soft);
/* Ritar bara inom rektangeln tills gfx_clip_reset */
void gfx_clip(float x, float y, float w, float h);
void gfx_clip_reset(void);
void gfx_stroke(const float *xy, const u32 *colors, int n, float width);

enum { BTN_CROSS, BTN_CIRCLE, BTN_TRIANGLE, BTN_SQUARE };
/* Egenritad knappsymbol i en rund platta */
void gfx_button(float cx, float cy, float r, int kind, u32 plate, u32 mark);

Texture *tex_create(int w, int h, int psm);
void tex_free(Texture *t);
void tex_flush(Texture *t); /* skriv tillbaka dcache efter CPU-ändringar */

/* Text (intraFont, systemtypsnitt läses från flash0). `need_jpn` laddar japanskt alternativtypsnitt. */
void font_init(int need_jpn);
void font_term(void);
/* Laddar det japanska typsnittet (kanji, kana, kyrilliska) om det inte redan är laddat. */
void font_ensure_cjk(void);
float text_width(float size, int bold, const char *s);
void text_draw(float x, float y, float size, int bold, u32 color, int align, const char *s);
/* Största storlek ≤ size (ner till min_size) där texten ryms inom max_w */
float text_fit(float size, float min_size, int bold, float max_w, const char *s);

#endif
