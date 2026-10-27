/* psp5 – made by 08Weber */
#include <pspkernel.h>
#include <png.h>
#include <stdio.h>
#include <setjmp.h>
#include <jpeglib.h>
#include <malloc.h>
#include <math.h>
#include <string.h>
#include "image.h"

#define NTX_MAGIC 0x3158544E /* "NTX1" */
#define MAX_PNG_PIXELS (512 * 512)
#define MAX_COVER_PIXELS (1024 * 1024) /* omslag är ofta större än ikoner */
#define MAX_PROGRESSIVE_PIXELS (1600 * 1600) /* progressiv JPEG avkodas i sin helhet */

typedef struct {
    u32 magic;
    u16 w, h;
    u16 psm, stride;
    u32 reserved;
} NtxHeader;

typedef struct {
    const u8 *data;
    u32 size, pos;
} PngMem;

static void png_read_mem(png_structp png, png_bytep dst, png_size_t len)
{
    PngMem *m = png_get_io_ptr(png);
    if (m->pos + len > m->size)
        png_error(png, "eof");
    memcpy(dst, m->data + m->pos, len);
    m->pos += len;
}

/* Stora PNG-bilder (t.ex. 1080p/4K-skärmdumpar eller bakgrundsbilder i filhanteraren, issue #14)
 * skalas ner rad för rad under avkodningen så att hela bilden inte behöver ligga i RAM. */
static int decode_png_scaled(const void *data, u32 size, Image *out, u32 max_pixels)
{
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png)
        return 0;
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        return 0;
    }
    u8 *volatile row = NULL;
    out->px = NULL;
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        free((void *)row);
        img_free(out);
        return 0;
    }
    PngMem mem = { data, size, 0 };
    png_set_read_fn(png, &mem, png_read_mem);
    png_read_info(png, info);
    u32 w = png_get_image_width(png, info), h = png_get_image_height(png, info);
    if (!w || !h || w > 8192 || h > 8192 || png_get_interlace_type(png, info) != PNG_INTERLACE_NONE)
        longjmp(png_jmpbuf(png), 1);
    png_set_expand(png);
    png_set_strip_16(png);
    png_set_gray_to_rgb(png);
    png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
    png_read_update_info(png, info);

    int step = 1;
    while ((w / step) * (h / step) > max_pixels)
        step *= 2;
    int dw = w / step, dh = h / step;
    if (dw < 1 || dh < 1)
        longjmp(png_jmpbuf(png), 1);
    out->w = dw;
    out->h = dh;
    out->px = malloc(dw * dh * 4);
    row = malloc(w * 4);
    if (!out->px || !row)
        longjmp(png_jmpbuf(png), 1);
    for (u32 y = 0; y < h; y++) {
        png_read_row(png, row, NULL);
        if (y % step == 0 && (int)(y / step) < dh) {
            u32 *dst = out->px + (y / step) * dw;
            const u32 *src = (const u32 *)row;
            for (int x = 0; x < dw; x++)
                dst[x] = src[x * step];
        }
    }
    png_destroy_read_struct(&png, &info, NULL);
    free((void *)row);
    return 1;
}

static int decode_png(const void *data, u32 size, Image *out, u32 max_pixels)
{
    png_image png;
    memset(&png, 0, sizeof(png));
    png.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&png, data, size))
        return 0;
    if (png.width * png.height > max_pixels) {
        png_image_free(&png);
        return decode_png_scaled(data, size, out, max_pixels);
    }
    png.format = PNG_FORMAT_RGBA; /* R,G,B,A i minnet = ABGR som u32 på little endian */
    out->w = png.width;
    out->h = png.height;
    out->px = malloc(png.width * png.height * 4);
    if (!out->px) {
        png_image_free(&png);
        return 0;
    }
    if (!png_image_finish_read(&png, NULL, out->px, 0, NULL)) {
        img_free(out);
        return 0;
    }
    return 1;
}

int img_decode_png(const void *data, u32 size, Image *out)
{
    return decode_png(data, size, out, MAX_PNG_PIXELS);
}

/* libjpeg avslutar programmet vid fel om man inte tar över felhanteringen */
typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf jump;
} JpegError;

static void jpeg_fail(j_common_ptr cinfo)
{
    longjmp(((JpegError *)cinfo->err)->jump, 1);
}

static void jpeg_quiet(j_common_ptr cinfo, int level) {}

static int decode_jpeg(const void *data, u32 size, Image *out, int min_side)
{
    struct jpeg_decompress_struct cinfo;
    JpegError err;
    u8 *volatile row = NULL; /* ändras efter setjmp – måste överleva longjmp */
    out->px = NULL;
    cinfo.err = jpeg_std_error(&err.pub);
    err.pub.error_exit = jpeg_fail;
    err.pub.emit_message = jpeg_quiet;
    if (setjmp(err.jump)) {
        jpeg_destroy_decompress(&cinfo);
        free((void *)row);
        img_free(out);
        return 0;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (const unsigned char *)data, size);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK ||
        (cinfo.progressive_mode && cinfo.image_width * cinfo.image_height > MAX_PROGRESSIVE_PIXELS)) {
        jpeg_destroy_decompress(&cinfo);
        return 0;
    }
    /* skala ner redan i avkodaren (1/2, 1/4, 1/8) så länge bilden blir minst min_side */
    int side = cinfo.image_width < cinfo.image_height ? cinfo.image_width : cinfo.image_height;
    cinfo.scale_num = 1;
    cinfo.scale_denom = 1;
    while (cinfo.scale_denom < 8 && side / (int)(cinfo.scale_denom * 2) >= min_side)
        cinfo.scale_denom *= 2;
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);
    int w = cinfo.output_width, h = cinfo.output_height;
    if ((u32)(w * h) > MAX_COVER_PIXELS || cinfo.output_components != 3)
        longjmp(err.jump, 1);
    out->w = w;
    out->h = h;
    out->px = malloc(w * h * 4);
    row = malloc(w * 3);
    if (!out->px || !row)
        longjmp(err.jump, 1);
    while (cinfo.output_scanline < cinfo.output_height) {
        int y = cinfo.output_scanline;
        JSAMPROW rows[1] = { row };
        jpeg_read_scanlines(&cinfo, rows, 1);
        for (int x = 0; x < w; x++)
            out->px[y * w + x] = RGBA(row[x * 3], row[x * 3 + 1], row[x * 3 + 2], 255);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    free((void *)row);
    return 1;
}

static u16 le16(const u8 *p) { return p[0] | (p[1] << 8); }
static u32 le32(const u8 *p) { return p[0] | (p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

/* BMP (24/32/16/8-bitars, t.ex. skärmdumpar från PSP-plugins). Skalas ner vid behov. */
static int decode_bmp(const void *data, u32 size, Image *out, u32 max_pixels)
{
    const u8 *b = data;
    if (size < 54 || b[0] != 'B' || b[1] != 'M')
        return 0;
    u32 off = le32(b + 10), dib = le32(b + 14);
    int w = (int)le32(b + 18), h_raw = (int)le32(b + 22);
    int top_down = h_raw < 0, h = top_down ? -h_raw : h_raw;
    u16 planes = le16(b + 26), bpp = le16(b + 28);
    u32 comp = le32(b + 30);
    if (dib < 40 || planes != 1 || w <= 0 || h <= 0 || w > 8192 || h > 8192 || off >= size)
        return 0;
    if ((comp != 0 && comp != 3) || (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32))
        return 0;
    u32 row_bytes = ((u32)w * bpp + 31) / 32 * 4;
    if (off + (u64)row_bytes * h > size)
        return 0;
    const u8 *pal = b + 14 + dib;
    u32 pal_end = 14 + dib + 256 * 4;
    if (bpp == 8 && pal_end > size)
        return 0;

    int step = 1;
    while ((u32)(w / step) * (u32)(h / step) > max_pixels)
        step *= 2;
    int dw = w / step, dh = h / step;
    if (dw < 1 || dh < 1)
        return 0;
    out->w = dw;
    out->h = dh;
    out->px = malloc(dw * dh * 4);
    if (!out->px)
        return 0;

    int r565 = comp == 3 && dib >= 52 && le32(b + 42) == 0x07E0;
    for (int dy = 0; dy < dh; dy++) {
        int sy = dy * step;
        int row_idx = top_down ? sy : (h - 1 - sy);
        const u8 *src = b + off + (u32)row_idx * row_bytes;
        u32 *dst = out->px + dy * dw;
        for (int dx = 0; dx < dw; dx++) {
            int sx = dx * step;
            u8 r = 0, g = 0, bl = 0;
            if (bpp == 24 || bpp == 32) {
                const u8 *p = src + sx * (bpp / 8);
                bl = p[0]; g = p[1]; r = p[2];
            } else if (bpp == 16) {
                u16 v = le16(src + sx * 2);
                if (r565) {
                    r = ((v >> 11) & 31) * 255 / 31;
                    g = ((v >> 5) & 63) * 255 / 63;
                    bl = (v & 31) * 255 / 31;
                } else {
                    r = ((v >> 10) & 31) * 255 / 31;
                    g = ((v >> 5) & 31) * 255 / 31;
                    bl = (v & 31) * 255 / 31;
                }
            } else if (bpp == 8) {
                const u8 *c = pal + src[sx] * 4;
                bl = c[0]; g = c[1]; r = c[2];
            }
            dst[dx] = RGBA(r, g, bl, 255);
        }
    }
    return 1;
}

int img_decode_cover(const void *data, u32 size, Image *out, int min_side)
{
    const u8 *b = data;
    if (size >= 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G')
        return decode_png(data, size, out, MAX_COVER_PIXELS);
    if (size >= 4 && b[0] == 0xFF && b[1] == 0xD8)
        return decode_jpeg(data, size, out, min_side);
    if (size >= 54 && b[0] == 'B' && b[1] == 'M')
        return decode_bmp(data, size, out, MAX_COVER_PIXELS);
    return 0;
}

int img_crop_square(Image *img)
{
    int side = img->w < img->h ? img->w : img->h;
    if (img->w == img->h)
        return 1;
    u32 *px = malloc(side * side * 4);
    if (!px)
        return 0;
    int x0 = (img->w - side) / 2, y0 = (img->h - side) / 2;
    for (int y = 0; y < side; y++)
        memcpy(px + y * side, img->px + (y + y0) * img->w + x0, side * 4);
    free(img->px);
    img->px = px;
    img->w = img->h = side;
    return 1;
}

void img_free(Image *img)
{
    free(img->px);
    img->px = NULL;
}

#define CH(c, s) (((c) >> (s)) & 0xFF)

int img_resize(const Image *src, int dw, int dh, Image *out)
{
    out->w = dw;
    out->h = dh;
    out->px = malloc(dw * dh * 4);
    if (!out->px)
        return 0;

    float sx = (float)src->w / dw, sy = (float)src->h / dh;
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            u32 acc[4] = { 0 };
            u32 r;
            if (sx > 1.0f || sy > 1.0f) {
                /* förminskning: medelvärde av källrutan */
                int x0 = x * sx, x1 = (x + 1) * sx, y0 = y * sy, y1 = (y + 1) * sy;
                if (x1 <= x0) x1 = x0 + 1;
                if (y1 <= y0) y1 = y0 + 1;
                int n = 0;
                for (int yy = y0; yy < y1 && yy < src->h; yy++)
                    for (int xx = x0; xx < x1 && xx < src->w; xx++, n++) {
                        u32 c = src->px[yy * src->w + xx];
                        for (int k = 0; k < 4; k++)
                            acc[k] += CH(c, k * 8);
                    }
                if (!n) n = 1;
                r = 0;
                for (int k = 0; k < 4; k++)
                    r |= (acc[k] / n) << (k * 8);
            } else {
                /* förstoring: bilinjär */
                float fx = (x + 0.5f) * sx - 0.5f, fy = (y + 0.5f) * sy - 0.5f;
                if (fx < 0) fx = 0;
                if (fy < 0) fy = 0;
                int ix = fx, iy = fy;
                int ix1 = ix + 1 < src->w ? ix + 1 : ix, iy1 = iy + 1 < src->h ? iy + 1 : iy;
                float ax = fx - ix, ay = fy - iy;
                u32 c00 = src->px[iy * src->w + ix], c10 = src->px[iy * src->w + ix1];
                u32 c01 = src->px[iy1 * src->w + ix], c11 = src->px[iy1 * src->w + ix1];
                r = 0;
                for (int k = 0; k < 4; k++) {
                    float top = CH(c00, k * 8) * (1 - ax) + CH(c10, k * 8) * ax;
                    float bot = CH(c01, k * 8) * (1 - ax) + CH(c11, k * 8) * ax;
                    r |= ((u32)(top * (1 - ay) + bot * ay + 0.5f)) << (k * 8);
                }
            }
            out->px[y * dw + x] = r;
        }
    }
    return 1;
}

/* En boxoskärpa i en riktning (src sammanhängande, dst med steget `step`);
 * tre pass i varje led ≈ gaussisk oskärpa. */
static void box_pass(u32 *dst, const u32 *src, int len, int step, int radius)
{
    int win = radius * 2 + 1;
    u32 sum[4] = { 0 };
    for (int i = -radius; i <= radius; i++) {
        int j = i < 0 ? 0 : i >= len ? len - 1 : i;
        for (int k = 0; k < 4; k++)
            sum[k] += CH(src[j], k * 8);
    }
    for (int i = 0; i < len; i++) {
        u32 r = 0;
        for (int k = 0; k < 4; k++)
            r |= (sum[k] / win) << (k * 8);
        dst[i * step] = r;
        int out = i - radius < 0 ? 0 : i - radius;
        int in = i + radius + 1 >= len ? len - 1 : i + radius + 1;
        for (int k = 0; k < 4; k++)
            sum[k] += CH(src[in], k * 8) - CH(src[out], k * 8);
    }
}

void img_blur(Image *img, int radius)
{
    int n = img->w > img->h ? img->w : img->h;
    u32 *line = malloc(n * 4);
    if (!line || radius < 1) {
        free(line);
        return;
    }
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < img->h; y++) {
            u32 *row = img->px + y * img->w;
            memcpy(line, row, img->w * 4);
            box_pass(row, line, img->w, 1, radius);
        }
        for (int x = 0; x < img->w; x++) {
            for (int y = 0; y < img->h; y++)
                line[y] = img->px[y * img->w + x];
            box_pass(img->px + x, line, img->h, img->w, radius);
        }
    }
    free(line);
}

void img_round_corners(Image *img, float radius)
{
    for (int y = 0; y < img->h; y++) {
        for (int x = 0; x < img->w; x++) {
            float cx = x + 0.5f < radius ? radius : x + 0.5f > img->w - radius ? img->w - radius : -1;
            float cy = y + 0.5f < radius ? radius : y + 0.5f > img->h - radius ? img->h - radius : -1;
            if (cx < 0 || cy < 0)
                continue;
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float cover = radius - sqrtf(dx * dx + dy * dy) + 0.5f;
            if (cover >= 1.0f)
                continue;
            if (cover < 0)
                cover = 0;
            u32 *p = &img->px[y * img->w + x];
            *p = (*p & 0x00FFFFFF) | ((u32)(CH(*p, 24) * cover) << 24);
        }
    }
}

u32 img_average(const Image *img)
{
    u32 sum[3] = { 0 }, n = 0;
    for (int i = 0; i < img->w * img->h; i += 3, n++)
        for (int k = 0; k < 3; k++)
            sum[k] += CH(img->px[i], k * 8);
    if (!n)
        return RGBA(80, 80, 90, 255);
    return RGBA(sum[0] / n, sum[1] / n, sum[2] / n, 255);
}

Texture *img_to_texture(const Image *img, int psm)
{
    Texture *t = tex_create(img->w, img->h, psm);
    if (!t)
        return NULL;
    for (int y = 0; y <= img->h; y++) {
        const u32 *src = img->px + (y < img->h ? y : img->h - 1) * img->w; /* extraraden = sista raden */
        if (psm == GU_PSM_8888) {
            memcpy((u32 *)t->data + y * t->stride, src, img->w * 4);
        } else {
            u16 *dst = (u16 *)t->data + y * t->stride;
            for (int x = 0; x < img->w; x++) {
                u32 c = src[x];
                dst[x] = (CH(c, 0) >> 3) | ((CH(c, 8) >> 2) << 5) | ((CH(c, 16) >> 3) << 11);
            }
        }
    }
    tex_flush(t);
    return t;
}

Texture *ntx_load(const char *path)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return NULL;
    NtxHeader h;
    Texture *t = NULL;
    if (sceIoRead(fd, &h, sizeof(h)) == sizeof(h) && h.magic == NTX_MAGIC &&
        (h.psm == GU_PSM_8888 || h.psm == GU_PSM_5650) && h.w && h.h && h.w <= 512 && h.h <= 512) {
        t = tex_create(h.w, h.h, h.psm);
        if (t && t->stride == h.stride) {
            int bpp = h.psm == GU_PSM_8888 ? 4 : 2;
            int bytes = t->stride * h.h * bpp;
            if (sceIoRead(fd, t->data, bytes) == bytes) {
                /* extraraden = kopia av sista raden */
                memcpy((u8 *)t->data + bytes, (u8 *)t->data + bytes - t->stride * bpp, t->stride * bpp);
                tex_flush(t);
            } else {
                tex_free(t);
                t = NULL;
            }
        } else {
            tex_free(t);
            t = NULL;
        }
    }
    sceIoClose(fd);
    return t;
}

int ntx_save(const char *path, const Texture *t)
{
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0)
        return 0;
    NtxHeader h = { NTX_MAGIC, t->w, t->h, t->psm, t->stride, 0 };
    int bytes = t->stride * t->h * (t->psm == GU_PSM_8888 ? 4 : 2);
    int ok = sceIoWrite(fd, &h, sizeof(h)) == sizeof(h) && sceIoWrite(fd, t->data, bytes) == bytes;
    sceIoClose(fd);
    if (!ok)
        sceIoRemove(path);
    return ok;
}

typedef struct {
    SceUID fd;
    u8 buf[16384];
    u32 len;
} PngWrite;

static void png_write_cb(png_structp png, png_bytep data, png_size_t len)
{
    PngWrite *w = png_get_io_ptr(png);
    while (len > 0) {
        u32 room = sizeof(w->buf) - w->len;
        u32 chunk = len < room ? (u32)len : room;
        memcpy(w->buf + w->len, data, chunk);
        w->len += chunk;
        data += chunk;
        len -= chunk;
        if (w->len == sizeof(w->buf)) {
            if (sceIoWrite(w->fd, w->buf, w->len) != (int)w->len)
                png_error(png, "io");
            w->len = 0;
        }
    }
}

static void png_flush_cb(png_structp png) {}

int img_save_png(const char *path, const u32 *px, int w, int h, int stride)
{
    PngWrite *pw = malloc(sizeof(PngWrite));
    u8 *volatile row = malloc(w * 3);
    if (!pw || !row) {
        free(pw);
        free((void *)row);
        return 0;
    }
    pw->fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    pw->len = 0;
    if (pw->fd < 0) {
        free(pw);
        free((void *)row);
        return 0;
    }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        if (png)
            png_destroy_write_struct(&png, info ? &info : NULL);
        sceIoClose(pw->fd);
        sceIoRemove(path);
        free(pw);
        free((void *)row);
        return 0;
    }
    png_set_write_fn(png, pw, png_write_cb, png_flush_cb);
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < h; y++) {
        const u32 *src = px + y * stride;
        for (int x = 0; x < w; x++) {
            u32 c = src[x];
            row[x * 3] = c & 0xFF;
            row[x * 3 + 1] = (c >> 8) & 0xFF;
            row[x * 3 + 2] = (c >> 16) & 0xFF;
        }
        png_write_row(png, row);
    }
    png_write_end(png, NULL);
    if (pw->len > 0 && sceIoWrite(pw->fd, pw->buf, pw->len) != (int)pw->len)
        longjmp(png_jmpbuf(png), 1);
    png_destroy_write_struct(&png, &info);
    sceIoClose(pw->fd);
    free(pw);
    free((void *)row);
    return 1;
}
