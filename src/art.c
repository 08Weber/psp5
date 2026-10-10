/* psp5 – made by Mille Weber */
/* Hämtar kort- och bakgrundsbilder. Ordning:
 *   1. covers/<nyckel>.ico|.bg  (egna omslag från PC-skriptet)
 *   2. cache/<hash>.ico|.bg     (tidigare bearbetade)
 *   3. ICON0/PIC1 ur spelet → bearbetas → sparas i cache/ */
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "art.h"
#include "image.h"
#include "iso.h"

#define MAX_PNG_SIZE (512 * 1024)
#define ICON_RADIUS 7.0f

static char s_cache_dir[200];
static char s_cover_dir[200];

void art_init(const char *base_dir)
{
    snprintf(s_cache_dir, sizeof(s_cache_dir), "%s/cache", base_dir);
    snprintf(s_cover_dir, sizeof(s_cover_dir), "%s/covers", base_dir);
    sceIoMkdir(s_cache_dir, 0777);
    sceIoMkdir(s_cover_dir, 0777);
}

static u32 fnv1a(const char *s, u32 h)
{
    while (*s)
        h = (h ^ (u8)*s++) * 16777619u;
    return h;
}

static void cache_path(const Entry *e, const char *ext, char *out, int size)
{
    u32 h = fnv1a(e->path, 2166136261u) ^ e->file_size ^ (e->file_time * 2654435761u);
    snprintf(out, size, "%s/%08X.%s", s_cache_dir, (unsigned)h, ext);
}

static void cover_path(const Entry *e, const char *ext, char *out, int size)
{
    char key[96];
    entry_cover_key(e, key, sizeof(key));
    snprintf(out, size, "%s/%s.%s", s_cover_dir, key, ext);
}

/* Läser en inbäddad fil (ICON0/PIC1) ur ISO eller PBP. */
static void *read_embedded(const Entry *e, u32 offset, u32 size)
{
    if (!size || size > MAX_PNG_SIZE)
        return NULL;
    void *buf = malloc(size);
    if (!buf)
        return NULL;

    int got = -1;
    if (e->type == ENTRY_ISO) {
        IsoReader r;
        if (iso_open(&r, e->path) >= 0) {
            got = iso_read(&r, buf, size, offset);
            iso_close(&r);
        }
    } else {
        SceUID fd = sceIoOpen(e->path, PSP_O_RDONLY, 0);
        if (fd >= 0) {
            sceIoLseek32(fd, offset, PSP_SEEK_SET);
            got = sceIoRead(fd, buf, size);
            sceIoClose(fd);
        }
    }
    if (got != (int)size) {
        free(buf);
        return NULL;
    }
    return buf;
}

static int decode_embedded(const Entry *e, u32 offset, u32 size, Image *out)
{
    void *png = read_embedded(e, offset, size);
    if (!png)
        return 0;
    int ok = img_decode_png(png, size, out);
    free(png);
    return ok;
}

/* Skalar om bilden till exakt w×h om den inte redan har den storleken. */
static int fit(Image *img, int w, int h)
{
    if (img->w == w && img->h == h)
        return 1;
    Image scaled;
    if (!img_resize(img, w, h, &scaled))
        return 0;
    img_free(img);
    *img = scaled;
    return 1;
}

static u32 texture_average(const Texture *t)
{
    Image view = { t->w, t->h, NULL };
    /* 8888-texturens rader har stride ≥ w; räkna på en tät kopia */
    view.px = malloc(t->w * t->h * 4);
    if (!view.px)
        return RGBA(80, 80, 90, 255);
    for (int y = 0; y < t->h; y++)
        memcpy(view.px + y * t->w, (u32 *)t->data + y * t->stride, t->w * 4);
    u32 c = img_average(&view);
    img_free(&view);
    return c;
}

Texture *art_load_icon(const Entry *e, u32 *accent)
{
    char path[256];
    Texture *t;

    cover_path(e, "ico", path, sizeof(path));
    t = ntx_load(path);
    if (!t) {
        cache_path(e, "ico", path, sizeof(path));
        t = ntx_load(path);
    }
    if (!t) {
        Image img;
        if (!decode_embedded(e, e->icon_offset, e->icon_size, &img))
            return NULL;
        if (fit(&img, ICON_W, ICON_H)) {
            img_round_corners(&img, ICON_RADIUS);
            t = img_to_texture(&img, GU_PSM_8888);
            if (t)
                ntx_save(path, t);
        }
        img_free(&img);
    }
    if (t && t->psm != GU_PSM_8888) {
        tex_free(t);
        return NULL;
    }
    if (t && accent)
        *accent = texture_average(t);
    return t;
}

Texture *art_load_bg(const Entry *e)
{
    char path[256];
    Texture *t;

    cover_path(e, "bg", path, sizeof(path));
    if ((t = ntx_load(path)))
        return t;
    cache_path(e, "bg", path, sizeof(path));
    if ((t = ntx_load(path)))
        return t;

    Image img;
    int blur;
    if (decode_embedded(e, e->pic_offset, e->pic_size, &img)) {
        blur = 0; /* PIC1 visas skarp, som en stor omslagsbild */
    } else if (decode_embedded(e, e->icon_offset, e->icon_size, &img)) {
        blur = 10; /* ikonen förstoras – kraftig oskärpa ger en mjuk färgyta */
    } else {
        return NULL;
    }

    if (fit(&img, BG_W, BG_H)) {
        img_blur(&img, blur);
        t = img_to_texture(&img, GU_PSM_5650);
        if (t)
            ntx_save(path, t);
    }
    img_free(&img);
    return t;
}
