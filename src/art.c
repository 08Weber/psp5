/* psp5 – made by 08Weber */
/* Hämtar kort- och bakgrundsbilder. Ordning:
 *   1. covers/<nyckel>.ico|.bg  (egna omslag från PC-skriptet)
 *   2. cache/<hash>.ico|.bg     (tidigare bearbetade)
 *   3. ICON0/PIC1 ur spelet (eller UMD-skivan) → bearbetas → sparas i cache/ */
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "art.h"
#include "image.h"
#include "iso.h"
#include "umd.h"
#include "music.h"

#define MAX_PNG_SIZE (512 * 1024)
#define ICON_RADIUS 7.0f
#define COVER_RADIUS 8.0f /* som rutan i musikfliken */
#define MAX_COVER_FILE (2 * 1024 * 1024)

static char s_cache_dir[200];
static char s_cover_dir[200];

void art_init(const char *base_dir)
{
    snprintf(s_cache_dir, sizeof(s_cache_dir), "%s/cache", base_dir);
    snprintf(s_cover_dir, sizeof(s_cover_dir), "%s/covers", base_dir);
    sceIoMkdir(s_cache_dir, 0777);
    sceIoMkdir(s_cover_dir, 0777);

    /* 1.6.0: fyrkantiga ikoner (minis) sträcks inte längre – ta bort cachade ikoner en gång */
    char mark[220], path[256];
    SceIoStat st;
    snprintf(mark, sizeof(mark), "%s/icons2", s_cache_dir);
    if (sceIoGetstat(mark, &st) >= 0)
        return;
    SceUID d = sceIoDopen(s_cache_dir);
    if (d >= 0) {
        SceIoDirent ent;
        memset(&ent, 0, sizeof(ent));
        while (sceIoDread(d, &ent) > 0) {
            int n = strlen(ent.d_name);
            if (n > 4 && !strcasecmp(ent.d_name + n - 4, ".ico")) {
                snprintf(path, sizeof(path), "%s/%s", s_cache_dir, ent.d_name);
                sceIoRemove(path);
            }
            memset(&ent, 0, sizeof(ent));
        }
        sceIoDclose(d);
    }
    SceUID fd = sceIoOpen(mark, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0)
        sceIoClose(fd);
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
    if (e->type == ENTRY_UMD) /* alla skivor har samma sökväg */
        h = fnv1a(e->title, fnv1a(e->game_id, h));
    snprintf(out, size, "%s/%08X.%s", s_cache_dir, (unsigned)h, ext);
}

static void cover_path(const Entry *e, const char *ext, char *out, int size)
{
    char key[96];
    entry_cover_key(e, key, sizeof(key));
    snprintf(out, size, "%s/%s.%s", s_cover_dir, key, ext);
}

/* Läser en inbäddad fil (ICON0/PIC1/ICON1/SND0) ur ISO eller PBP – eller filen `umd_name` på UMD-skivan. */
void *art_read_part(const Entry *e, u32 offset, u32 size, const char *umd_name, u32 max_size)
{
    if (!size || size > max_size)
        return NULL;
    void *buf = malloc(size);
    if (!buf)
        return NULL;

    int got = -1;
    char umd_path[64];
    if (e->type == ENTRY_UMD) {
        snprintf(umd_path, sizeof(umd_path), UMD_ROOT "PSP_GAME/%s", umd_name);
        SceUID fd = sceIoOpen(umd_path, PSP_O_RDONLY, 0);
        if (fd >= 0) {
            got = sceIoRead(fd, buf, size);
            sceIoClose(fd);
        }
    } else if (e->type == ENTRY_ISO) {
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

static int decode_embedded(const Entry *e, u32 offset, u32 size, const char *umd_name, Image *out)
{
    void *png = art_read_part(e, offset, size, umd_name, MAX_PNG_SIZE);
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

/* Ikon till kortets format. En ikon som är mycket smalare än 144×80 (t.ex. PSP minis
 * fyrkantiga ikoner) sträcks inte: den centreras på en suddig, mörkare kopia av sig själv. */
static int fit_icon(Image *img)
{
    if (img->h <= 0 || img->w * 10 >= img->h * 15) /* bredare än 1,5:1 – vanlig ikon */
        return fit(img, ICON_W, ICON_H);
    int fw = img->w * ICON_H / img->h;
    Image bg, fg;
    if (fw < 1 || !img_resize(img, ICON_W, ICON_H, &bg))
        return 0;
    if (!img_resize(img, fw, ICON_H, &fg)) {
        img_free(&bg);
        return 0;
    }
    img_blur(&bg, 8);
    for (int i = 0; i < ICON_W * ICON_H; i++) { /* 55 % ljus, helt ogenomskinlig */
        u32 c = bg.px[i];
        bg.px[i] = RGBA((c & 0xFF) * 55 / 100, ((c >> 8) & 0xFF) * 55 / 100, ((c >> 16) & 0xFF) * 55 / 100, 255);
    }
    int x0 = (ICON_W - fw) / 2;
    for (int y = 0; y < ICON_H; y++) {
        for (int x = 0; x < fw; x++) {
            u32 f = fg.px[y * fw + x], *b = &bg.px[y * ICON_W + x0 + x];
            u32 a = f >> 24, out = 0xFF000000;
            for (int s = 0; s < 24; s += 8)
                out |= ((((f >> s) & 0xFF) * a + ((*b >> s) & 0xFF) * (255 - a)) / 255) << s;
            *b = out;
        }
    }
    img_free(&fg);
    img_free(img);
    *img = bg;
    return 1;
}

/* Medelfärgen direkt ur texturen (rader med stride), var tredje pixel som img_average –
 * utan att först kopiera hela ikonen. */
static u32 texture_average(const Texture *t)
{
    const u32 *px = t->data;
    u32 sum[3] = { 0 }, n = 0;
    int x = 0, y = 0;
    for (int i = 0; i < t->w * t->h; i += 3, n++) {
        u32 c = px[y * t->stride + x];
        for (x += 3; x >= t->w; x -= t->w) /* samma pixlar som i en tät bild */
            y++;
        sum[0] += c & 0xFF;
        sum[1] += (c >> 8) & 0xFF;
        sum[2] += (c >> 16) & 0xFF;
    }
    if (!n)
        return RGBA(80, 80, 90, 255);
    return RGBA(sum[0] / n, sum[1] / n, sum[2] / n, 255);
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
        if (!decode_embedded(e, e->icon_offset, e->icon_size, "ICON0.PNG", &img))
            return NULL;
        if (fit_icon(&img)) {
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
    cache_path(e, "bg2", path, sizeof(path)); /* bg2: PIC1 i full storlek sedan 1.6.1 */
    if ((t = ntx_load(path)))
        return t;

    Image img;
    int blur, w = SCREEN_W, h = SCREEN_H;
    if (decode_embedded(e, e->pic_offset, e->pic_size, "PIC1.PNG", &img)) {
        blur = 0; /* PIC1 visas skarp i skärmens storlek, som en stor omslagsbild */
    } else if (decode_embedded(e, e->icon_offset, e->icon_size, "ICON0.PNG", &img)) {
        blur = 10; /* ikonen förstoras – kraftig oskärpa ger en mjuk färgyta, liten fil räcker */
        w = BG_W;
        h = BG_H;
    } else {
        return NULL;
    }

    if (fit(&img, w, h)) {
        img_blur(&img, blur);
        t = img_to_texture(&img, GU_PSM_5650);
        if (t)
            ntx_save(path, t);
    }
    img_free(&img);
    return t;
}

/* PIC0 som spelets logga: genomskinliga kanter skärs bort, så att själva loggan fyller rutan */
Texture *art_load_logo(const Entry *e)
{
    if (!e->pic0_size)
        return NULL;
    char path[256];
    cache_path(e, "p0", path, sizeof(path));
    Texture *t = ntx_load(path);
    if (t)
        return t;

    Image img;
    if (!decode_embedded(e, e->pic0_offset, e->pic0_size, "PIC0.PNG", &img))
        return NULL;
    int x0 = img.w, y0 = img.h, x1 = -1, y1 = -1;
    for (int y = 0; y < img.h; y++)
        for (int x = 0; x < img.w; x++)
            if ((img.px[y * img.w + x] >> 24) > 16) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
    Image crop = { cw, ch, cw > 0 && ch > 0 ? malloc(cw * ch * 4) : NULL };
    if (crop.px) {
        for (int y = 0; y < ch; y++)
            memcpy(crop.px + y * cw, img.px + (y + y0) * img.w + x0, cw * 4);
        int w = LOGO_W, h = ch * LOGO_W / cw; /* behåll formatet, rymmas i LOGO_W×LOGO_H */
        if (h > LOGO_H) {
            h = LOGO_H;
            w = cw * LOGO_H / ch;
        }
        if (w > 0 && h > 0 && fit(&crop, w, h)) {
            t = img_to_texture(&crop, GU_PSM_8888);
            if (t)
                ntx_save(path, t);
        }
        img_free(&crop);
    }
    img_free(&img);
    return t;
}

/* --- omslag till musik --- */

/* Bild som ligger bredvid låtarna i albummappen */
static void *folder_cover(const char *mp3_path, u32 *size)
{
    static const char *names[] = { "cover.jpg", "folder.jpg", "front.jpg", "album.jpg", "cover.png", "folder.png" };
    const char *slash = strrchr(mp3_path, '/');
    if (!slash)
        return NULL;
    char path[300];
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(path, sizeof(path), "%.*s/%s", (int)(slash - mp3_path), mp3_path, names[i]);
        SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
        if (fd < 0)
            continue;
        int n = sceIoLseek32(fd, 0, PSP_SEEK_END);
        void *buf = n > 0 && n <= MAX_COVER_FILE ? malloc(n) : NULL;
        if (buf) {
            sceIoLseek32(fd, 0, PSP_SEEK_SET);
            if (sceIoRead(fd, buf, n) != n) {
                free(buf);
                buf = NULL;
            }
        }
        sceIoClose(fd);
        if (buf) {
            *size = n;
            return buf;
        }
    }
    return NULL;
}

Texture *art_load_cover(const char *mp3_path, const char *album)
{
    /* ett album delar omslag (samma mapp och albumnamn); utan album gäller det låten */
    char path[256];
    const char *slash = strrchr(mp3_path, '/');
    u32 h = 2166136261u;
    if (album[0] && slash) {
        char folder[256];
        snprintf(folder, sizeof(folder), "%.*s", (int)(slash - mp3_path), mp3_path);
        h = fnv1a(album, fnv1a(folder, h));
    } else {
        h = fnv1a(mp3_path, h);
    }
    snprintf(path, sizeof(path), "%s/%08X.cov", s_cache_dir, (unsigned)h);
    Texture *t = ntx_load(path);
    if (t)
        return t;

    u32 size = 0;
    void *data = music_cover_data(mp3_path, &size);
    if (!data)
        data = folder_cover(mp3_path, &size);
    if (!data)
        return NULL;
    Image img;
    int ok = img_decode_cover(data, size, &img, COVER_SIZE);
    free(data); /* före skalningen – omslagsfilen kan vara stor */
    if (!ok)
        return NULL;
    if (img_crop_square(&img) && fit(&img, COVER_SIZE, COVER_SIZE)) {
        img_round_corners(&img, COVER_RADIUS);
        t = img_to_texture(&img, GU_PSM_8888);
        if (t)
            ntx_save(path, t);
    }
    img_free(&img);
    return t;
}

/* --- egen bakgrundsbild --- */

#define MAX_WALLPAPER_FILE (8 * 1024 * 1024)

Texture *art_load_wallpaper(const char *file)
{
    SceIoStat st;
    if (!file[0] || sceIoGetstat(file, &st) < 0 || st.st_size <= 0 || st.st_size > MAX_WALLPAPER_FILE)
        return NULL;
    /* cachen följer filen: ny bild med samma namn ger en ny cachefil */
    char path[256];
    u32 h = fnv1a(file, 2166136261u) ^ (u32)st.st_size ^ (st.sce_st_mtime.second * 2654435761u) ^
            (st.sce_st_mtime.minute << 8) ^ (st.sce_st_mtime.day << 16);
    snprintf(path, sizeof(path), "%s/%08X.wal", s_cache_dir, (unsigned)h);
    Texture *t = ntx_load(path);
    if (t)
        return t;

    u32 size = (u32)st.st_size;
    void *data = malloc(size);
    SceUID fd = data ? sceIoOpen(file, PSP_O_RDONLY, 0) : -1;
    int got = fd >= 0 ? sceIoRead(fd, data, size) : -1;
    if (fd >= 0)
        sceIoClose(fd);
    Image img;
    int ok = got == (int)size && img_decode_cover(data, size, &img, SCREEN_H);
    free(data);
    if (!ok)
        return NULL;

    /* beskär till skärmens format runt mitten och skala till skärmens storlek */
    int cw = img.w, ch = img.w * SCREEN_H / SCREEN_W;
    if (ch > img.h) {
        ch = img.h;
        cw = img.h * SCREEN_W / SCREEN_H;
    }
    Image crop = { cw, ch, malloc(cw * ch * 4) };
    if (crop.px) {
        int x0 = (img.w - cw) / 2, y0 = (img.h - ch) / 2;
        for (int y = 0; y < ch; y++)
            memcpy(crop.px + y * cw, img.px + (y + y0) * img.w + x0, cw * 4);
        if (fit(&crop, SCREEN_W, SCREEN_H)) {
            t = img_to_texture(&crop, GU_PSM_5650);
            if (t)
                ntx_save(path, t);
        }
        img_free(&crop);
    }
    img_free(&img);
    return t;
}
