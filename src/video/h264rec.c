/* psp5 authentic – the H.264 decoder in software: making the picture (the transform, intra prediction, motion compensation). Written from the standard (ITU-T H.264, clauses 8.3 to 8.5). */
#include <string.h>

#include "h264int.h"

static inline uint8_t clip1(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

/* clipping by table: one load instead of two compares (the filters of the motion compensation give values from about -80 to 340) */
static uint8_t g_crop[1024];
static int g_crop_ready;
#define CRP(x) g_crop[(x) + 384]

static void crop_init(void)
{
    if (g_crop_ready)
        return;
    for (int i = 0; i < 1024; i++)
        g_crop[i] = clip1(i - 384);
    g_crop_ready = 1;
}

void h264_rec_init(void) { crop_init(); }

const uint8_t h264_chroma_qp[52] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
                                      29, 30, 31, 32, 32, 33, 34, 34, 35, 35, 36, 36, 37, 37, 37, 38, 38, 38, 39, 39, 39, 39 };

/* ---------------------------------------------------------------------- */
/* the transform */

/* the inverse 4x4 transform of c (raster order, already scaled), added to the picture; c is cleared. The rounding (+ 32) is added to the DC first: it reaches all 16 results with the weight 1, so the
 * "+ 32" of each result is not needed (the same values), and the shift of every result is the only thing left of the rounding. */
void h264_idct_add(uint8_t *dst, int stride, int16_t *c, int dc_only)
{
    if (dc_only) {
        int dc = (c[0] + 32) >> 6;
        c[0] = 0;
        for (int y = 0; y < 4; y++, dst += stride) {
            dst[0] = clip1(dst[0] + dc);
            dst[1] = clip1(dst[1] + dc);
            dst[2] = clip1(dst[2] + dc);
            dst[3] = clip1(dst[3] + dc);
        }
        return;
    }
    int t00, t01, t02, t03, t10, t11, t12, t13, t20, t21, t22, t23, t30, t31, t32, t33;
#define ROW(i, o0, o1, o2, o3, bias) \
    do { \
        int a = c[(i) * 4] + (bias), b = c[(i) * 4 + 1], cc = c[(i) * 4 + 2], d = c[(i) * 4 + 3]; \
        int e0 = a + cc, e1 = a - cc, e2 = (b >> 1) - d, e3 = b + (d >> 1); \
        o0 = e0 + e3; \
        o1 = e1 + e2; \
        o2 = e1 - e2; \
        o3 = e0 - e3; \
    } while (0)
    ROW(0, t00, t01, t02, t03, 32);
    ROW(1, t10, t11, t12, t13, 0);
    ROW(2, t20, t21, t22, t23, 0);
    ROW(3, t30, t31, t32, t33, 0);
#undef ROW
    memset(c, 0, 16 * sizeof(int16_t));
#define COL(j, a, b, cc, d) \
    do { \
        int e0 = (a) + (cc), e1 = (a) - (cc), e2 = ((b) >> 1) - (d), e3 = (b) + ((d) >> 1); \
        uint8_t *p = dst + (j); \
        p[0] = clip1(p[0] + ((e0 + e3) >> 6)); \
        p[stride] = clip1(p[stride] + ((e1 + e2) >> 6)); \
        p[2 * stride] = clip1(p[2 * stride] + ((e1 - e2) >> 6)); \
        p[3 * stride] = clip1(p[3 * stride] + ((e0 - e3) >> 6)); \
    } while (0)
    COL(0, t00, t10, t20, t30);
    COL(1, t01, t11, t21, t31);
    COL(2, t02, t12, t22, t32);
    COL(3, t03, t13, t23, t33);
#undef COL
}

static const uint8_t NORM0[6] = { 10, 11, 13, 14, 16, 18 };

/* the DC levels of an Intra16x16 macroblock (a 4x4 matrix, raster order, parsed) to the DC of the 16 blocks */
void h264_dequant_luma_dc(int16_t *dc, int qp)
{
    int t[16], s = qp / 6, n0 = NORM0[qp % 6];
    for (int i = 0; i < 4; i++) { /* rows, then columns, of the 4x4 Hadamard transform */
        int a = dc[i * 4], b = dc[i * 4 + 1], c = dc[i * 4 + 2], d = dc[i * 4 + 3];
        t[i * 4] = a + b + c + d;
        t[i * 4 + 1] = a + b - c - d;
        t[i * 4 + 2] = a - b - c + d;
        t[i * 4 + 3] = a - b + c - d;
    }
    for (int j = 0; j < 4; j++) {
        int a = t[j], b = t[4 + j], c = t[8 + j], d = t[12 + j];
        int f0 = a + b + c + d, f1 = a + b - c - d, f2 = a - b - c + d, f3 = a - b + c - d;
        dc[j] = (int16_t)(((f0 * n0 * (1 << s)) + 2) >> 2);
        dc[4 + j] = (int16_t)(((f1 * n0 * (1 << s)) + 2) >> 2);
        dc[8 + j] = (int16_t)(((f2 * n0 * (1 << s)) + 2) >> 2);
        dc[12 + j] = (int16_t)(((f3 * n0 * (1 << s)) + 2) >> 2);
    }
}

void h264_dequant_chroma_dc(int16_t *dc, int qp)
{
    int s = qp / 6, n0 = NORM0[qp % 6];
    int a = dc[0], b = dc[1], c = dc[2], d = dc[3];
    int f0 = a + b + c + d, f1 = a - b + c - d, f2 = a + b - c - d, f3 = a - b - c + d;
    dc[0] = (int16_t)((f0 * n0 * (1 << s)) >> 1);
    dc[1] = (int16_t)((f1 * n0 * (1 << s)) >> 1);
    dc[2] = (int16_t)((f2 * n0 * (1 << s)) >> 1);
    dc[3] = (int16_t)((f3 * n0 * (1 << s)) >> 1);
}

/* ---------------------------------------------------------------------- */
/* intra prediction */

void h264_intra4_pred(uint8_t *dst, int stride, int mode, int has_left, int has_top, int has_tl, int has_tr)
{
    /* e[0..3] = left from the bottom up, e[4] = top left, e[5..12] = top (4 above, 4 above right) */
    int e[13];
    for (int i = 0; i < 4; i++)
        e[3 - i] = has_left ? dst[i * stride - 1] : 128;
    e[4] = has_tl ? dst[-stride - 1] : 128;
    for (int i = 0; i < 4; i++)
        e[5 + i] = has_top ? dst[-stride + i] : 128;
    for (int i = 4; i < 8; i++)
        e[5 + i] = has_top ? (has_tr ? dst[-stride + i] : dst[-stride + 3]) : 128;
#define L(y) e[3 - (y)]
#define T(x) e[5 + (x)]
    uint8_t out[4][4];
    switch (mode) {
    case 0:
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                out[y][x] = (uint8_t)T(x);
        break;
    case 1:
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                out[y][x] = (uint8_t)L(y);
        break;
    case 2: {
        int v;
        if (has_left && has_top)
            v = (T(0) + T(1) + T(2) + T(3) + L(0) + L(1) + L(2) + L(3) + 4) >> 3;
        else if (has_left)
            v = (L(0) + L(1) + L(2) + L(3) + 2) >> 2;
        else if (has_top)
            v = (T(0) + T(1) + T(2) + T(3) + 2) >> 2;
        else
            v = 128;
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                out[y][x] = (uint8_t)v;
        break;
    }
    case 3:
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                out[y][x] = (x == 3 && y == 3) ? (uint8_t)((T(6) + 3 * T(7) + 2) >> 2) : (uint8_t)((T(x + y) + 2 * T(x + y + 1) + T(x + y + 2) + 2) >> 2);
        break;
    case 4:
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++) {
                if (x > y)
                    out[y][x] = (uint8_t)((T(x - y - 2) + 2 * T(x - y - 1) + T(x - y) + 2) >> 2); /* (T(-1) is the corner: e[4]) */
                else if (x < y)
                    out[y][x] = (uint8_t)((L(y - x - 2) + 2 * L(y - x - 1) + L(y - x) + 2) >> 2);
                else
                    out[y][x] = (uint8_t)((T(0) + 2 * e[4] + L(0) + 2) >> 2);
            }
        break;
    case 5:
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++) {
                int z = 2 * x - y, k = x - (y >> 1);
                if (z >= 0 && !(z & 1))
                    out[y][x] = (uint8_t)((T(k - 1) + T(k) + 1) >> 1);
                else if (z >= 0)
                    out[y][x] = (uint8_t)((T(k - 2) + 2 * T(k - 1) + T(k) + 2) >> 2);
                else if (z == -1)
                    out[y][x] = (uint8_t)((L(0) + 2 * e[4] + T(0) + 2) >> 2);
                else
                    out[y][x] = (uint8_t)((L(y - 1) + 2 * L(y - 2) + L(y - 3) + 2) >> 2);
            }
        break;
    case 6:
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++) {
                int z = 2 * y - x, k = y - (x >> 1);
                if (z >= 0 && !(z & 1))
                    out[y][x] = (uint8_t)((L(k - 1) + L(k) + 1) >> 1);
                else if (z >= 0)
                    out[y][x] = (uint8_t)((L(k - 2) + 2 * L(k - 1) + L(k) + 2) >> 2);
                else if (z == -1)
                    out[y][x] = (uint8_t)((L(0) + 2 * e[4] + T(0) + 2) >> 2);
                else
                    out[y][x] = (uint8_t)((T(x - 1) + 2 * T(x - 2) + T(x - 3) + 2) >> 2);
            }
        break;
    case 7:
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++) {
                int k = x + (y >> 1);
                if (!(y & 1))
                    out[y][x] = (uint8_t)((T(k) + T(k + 1) + 1) >> 1);
                else
                    out[y][x] = (uint8_t)((T(k) + 2 * T(k + 1) + T(k + 2) + 2) >> 2);
            }
        break;
    default: /* 8 */
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++) {
                int z = x + 2 * y, k = y + (x >> 1);
                if (z > 5)
                    out[y][x] = (uint8_t)L(3);
                else if (z == 5)
                    out[y][x] = (uint8_t)((L(2) + 3 * L(3) + 2) >> 2);
                else if (!(z & 1))
                    out[y][x] = (uint8_t)((L(k) + L(k + 1) + 1) >> 1);
                else
                    out[y][x] = (uint8_t)((L(k) + 2 * L(k + 1) + L(k + 2) + 2) >> 2);
            }
        break;
    }
#undef L
#undef T
    for (int y = 0; y < 4; y++)
        memcpy(dst + y * stride, out[y], 4);
}

void h264_intra16_pred(uint8_t *dst, int stride, int mode, int has_left, int has_top)
{
    if (mode == 0) {
        for (int y = 0; y < 16; y++)
            memcpy(dst + y * stride, dst - stride, 16);
    } else if (mode == 1) {
        for (int y = 0; y < 16; y++)
            memset(dst + y * stride, dst[y * stride - 1], 16);
    } else if (mode == 2) {
        int sum = 0, v;
        if (has_top)
            for (int i = 0; i < 16; i++)
                sum += dst[-stride + i];
        if (has_left)
            for (int i = 0; i < 16; i++)
                sum += dst[i * stride - 1];
        if (has_top && has_left)
            v = (sum + 16) >> 5;
        else if (has_top || has_left)
            v = (sum + 8) >> 4;
        else
            v = 128;
        for (int y = 0; y < 16; y++)
            memset(dst + y * stride, v, 16);
    } else {
        int hh = 0, vv = 0;
        const uint8_t *top = dst - stride;
        for (int i = 0; i < 8; i++) {
            int a = top[8 + i], b = i == 7 ? top[-1] : top[6 - i];
            hh += (i + 1) * (a - b);
            int c = dst[(8 + i) * stride - 1], dd = i == 7 ? top[-1] : dst[(6 - i) * stride - 1];
            vv += (i + 1) * (c - dd);
        }
        int a = 16 * (dst[15 * stride - 1] + top[15]), b = (5 * hh + 32) >> 6, c = (5 * vv + 32) >> 6;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                dst[y * stride + x] = clip1((a + b * (x - 7) + c * (y - 7) + 16) >> 5);
    }
}

void h264_intra_chroma_pred(uint8_t *dst, int stride, int mode, int has_left, int has_top)
{
    if (mode == 0) { /* DC, block by block of 4x4 */
        for (int by = 0; by < 2; by++)
            for (int bx = 0; bx < 2; bx++) {
                int st = 0, sl = 0, v;
                if (has_top)
                    for (int i = 0; i < 4; i++)
                        st += dst[-stride + bx * 4 + i];
                if (has_left)
                    for (int i = 0; i < 4; i++)
                        sl += dst[(by * 4 + i) * stride - 1];
                if (bx == by) {
                    if (has_top && has_left) v = (st + sl + 4) >> 3;
                    else if (has_left) v = (sl + 2) >> 2;
                    else if (has_top) v = (st + 2) >> 2;
                    else v = 128;
                } else if (bx == 1) { /* top right: the top first */
                    if (has_top) v = (st + 2) >> 2;
                    else if (has_left) v = (sl + 2) >> 2;
                    else v = 128;
                } else { /* bottom left: the left first */
                    if (has_left) v = (sl + 2) >> 2;
                    else if (has_top) v = (st + 2) >> 2;
                    else v = 128;
                }
                for (int y = 0; y < 4; y++)
                    memset(dst + (by * 4 + y) * stride + bx * 4, v, 4);
            }
    } else if (mode == 1) {
        for (int y = 0; y < 8; y++)
            memset(dst + y * stride, dst[y * stride - 1], 8);
    } else if (mode == 2) {
        for (int y = 0; y < 8; y++)
            memcpy(dst + y * stride, dst - stride, 8);
    } else {
        int hh = 0, vv = 0;
        const uint8_t *top = dst - stride;
        for (int i = 0; i < 4; i++) {
            hh += (i + 1) * (top[4 + i] - (i == 3 ? top[-1] : top[2 - i]));
            vv += (i + 1) * (dst[(4 + i) * stride - 1] - (i == 3 ? top[-1] : dst[(2 - i) * stride - 1]));
        }
        int a = 16 * (dst[7 * stride - 1] + top[7]), b = (34 * hh + 32) >> 6, c = (34 * vv + 32) >> 6;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                dst[y * stride + x] = clip1((a + b * (x - 3) + c * (y - 3) + 16) >> 5);
    }
}

/* ---------------------------------------------------------------------- */
/* motion compensation */

/* a (w+5) x (h+5) window of the plane around (x - 2, y - 2) with the coordinates clamped into the picture (what the standard says of references outside of it) */
static const uint8_t *edge_window(const uint8_t *plane, int stride, int pw, int ph, int x, int y, int w, int h, uint8_t *tmp, int tstride)
{
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        yy = yy < 0 ? 0 : yy >= ph ? ph - 1 : yy;
        const uint8_t *row = plane + (ptrdiff_t)yy * stride;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            xx = xx < 0 ? 0 : xx >= pw ? pw - 1 : xx;
            tmp[j * tstride + i] = row[xx];
        }
    }
    return tmp;
}

#define TAP6(a, b, c, d, e, f) ((a) - 5 * (b) + 20 * (c) + 20 * (d) - 5 * (e) + (f))

/* the horizontal half position (clipped) of w x h pixels at src (2 before and 3 after readable) into dst; avg: -1 = as it is, 0 = averaged with the pixel at src, 1 = with the one after it (the quarter positions) */
static void put_h6(const uint8_t *src, int ss, uint8_t *dst, int ds, int w, int h, int avg)
{
    for (int y = 0; y < h; y++, src += ss, dst += ds) {
        const uint8_t *s = src - 2;
        int a = s[0], b = s[1], c = s[2], d = s[3], e = s[4];
        if (avg < 0) {
            for (int x = 0; x < w; x += 2) {
                int f = s[x + 5], g = s[x + 6];
                dst[x] = CRP((TAP6(a, b, c, d, e, f) + 16) >> 5);
                dst[x + 1] = CRP((TAP6(b, c, d, e, f, g) + 16) >> 5);
                a = c; b = d; c = e; d = f; e = g;
            }
        } else {
            const uint8_t *q = src + avg;
            for (int x = 0; x < w; x += 2) {
                int f = s[x + 5], g = s[x + 6];
                dst[x] = (uint8_t)((CRP((TAP6(a, b, c, d, e, f) + 16) >> 5) + q[x] + 1) >> 1);
                dst[x + 1] = (uint8_t)((CRP((TAP6(b, c, d, e, f, g) + 16) >> 5) + q[x + 1] + 1) >> 1);
                a = c; b = d; c = e; d = f; e = g;
            }
        }
    }
}

/* the same vertically (src: 2 rows before and 3 after readable); avg: -1, 0 = with the pixel at src, 1 = with the one below it */
static void put_v6(const uint8_t *src, int ss, uint8_t *dst, int ds, int w, int h, int avg)
{
    for (int x = 0; x < w; x++) {
        const uint8_t *s = src + x - 2 * ss;
        int a = s[0], b = s[ss], c = s[2 * ss], d = s[3 * ss], e = s[4 * ss];
        s += 5 * ss;
        uint8_t *o = dst + x;
        const uint8_t *q = src + x + avg * ss;
        for (int y = 0; y < h; y += 2) {
            int f = s[0], g = s[ss];
            int v0 = CRP((TAP6(a, b, c, d, e, f) + 16) >> 5), v1 = CRP((TAP6(b, c, d, e, f, g) + 16) >> 5);
            if (avg >= 0) {
                v0 = (v0 + q[y * ss] + 1) >> 1;
                v1 = (v1 + q[(y + 1) * ss] + 1) >> 1;
            }
            o[y * ds] = (uint8_t)v0;
            o[(y + 1) * ds] = (uint8_t)v1;
            a = c; b = d; c = e; d = f; e = g;
            s += 2 * ss;
        }
    }
}

/* src points at the whole pixel position, with 2 pixels before and 3 after it readable in both directions */
static void luma_qpel(const uint8_t *src, int ss, uint8_t *dst, int ds, int w, int h, int fx, int fy)
{
    if (!fx && !fy) { /* whole pixels: rows copied (with the size known, so that no call is made) */
        if (w == 16)
            for (int y = 0; y < 16; y++)
                memcpy(dst + y * ds, src + y * ss, 16);
        else if (w == 8)
            for (int y = 0; y < h; y++)
                memcpy(dst + y * ds, src + y * ss, 8);
        else
            for (int y = 0; y < h; y++)
                memcpy(dst + y * ds, src + y * ss, 4);
        return;
    }
    if (!fy) {
        put_h6(src, ss, dst, ds, w, h, fx == 2 ? -1 : fx == 3);
        return;
    }
    if (!fx) {
        put_v6(src, ss, dst, ds, w, h, fy == 2 ? -1 : fy == 3);
        return;
    }
    if (fx == 2 || fy == 2) { /* the centre position (and the ones that are an average of it with a half position) */
        int16_t mid[21 * 16];
        for (int y = -2; y < h + 3; y++) {
            const uint8_t *s = src + y * ss - 2;
            int16_t *m = mid + (y + 2) * 16;
            int a = s[0], b = s[1], c = s[2], d = s[3], e = s[4];
            for (int x = 0; x < w; x += 2) {
                int f = s[x + 5], g = s[x + 6];
                m[x] = (int16_t)TAP6(a, b, c, d, e, f);
                m[x + 1] = (int16_t)TAP6(b, c, d, e, f, g);
                a = c; b = d; c = e; d = f; e = g;
            }
        }
        if (fx == 2 && fy == 2) {
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    const int16_t *m = mid + (y + 2) * 16 + x;
                    dst[y * ds + x] = CRP((TAP6(m[-32], m[-16], m[0], m[16], m[32], m[48]) + 512) >> 10);
                }
        } else if (fx == 2) { /* with the horizontal half position of this row (fy = 1) or the next (fy = 3) */
            int off = (fy == 3) * 16;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    const int16_t *m = mid + (y + 2) * 16 + x;
                    int j = CRP((TAP6(m[-32], m[-16], m[0], m[16], m[32], m[48]) + 512) >> 10);
                    dst[y * ds + x] = (uint8_t)((j + CRP((m[off] + 16) >> 5) + 1) >> 1);
                }
        } else { /* fy == 2: with the vertical half position of this column (fx = 1) or the next (fx = 3) */
            int xo = fx == 3;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    const int16_t *m = mid + (y + 2) * 16 + x;
                    const uint8_t *s = src + y * ss + x + xo;
                    int j = CRP((TAP6(m[-32], m[-16], m[0], m[16], m[32], m[48]) + 512) >> 10);
                    int v = CRP((TAP6(s[-2 * ss], s[-ss], s[0], s[ss], s[2 * ss], s[3 * ss]) + 16) >> 5);
                    dst[y * ds + x] = (uint8_t)((j + v + 1) >> 1);
                }
        }
        return;
    }
    /* fx and fy are odd: the average of a horizontal and a vertical half position */
    uint8_t vb[16 * 16];
    put_h6(src + (fy == 3) * ss, ss, dst, ds, w, h, -1);
    put_v6(src + (fx == 3), ss, vb, 16, w, h, -1);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            dst[y * ds + x] = (uint8_t)((dst[y * ds + x] + vb[y * 16 + x] + 1) >> 1);
}

void h264_mc_luma(H264Dec *d, const H264Pic *ref, uint8_t *dst, int stride, int x, int y, int w, int h, int mvx, int mvy)
{
    int xi = x + (mvx >> 2), yi = y + (mvy >> 2), fx = mvx & 3, fy = mvy & 3;
    int pw = d->mbw * 16, ph = d->mbh * 16, ss = d->stride[0];
    const uint8_t *plane = ref->p[0];
    const uint8_t *src;
    if (xi - 2 >= -PAD_Y && yi - 2 >= -PAD_Y && xi + w + 3 <= pw + PAD_Y && yi + h + 3 <= ph + PAD_Y) {
        src = plane + (ptrdiff_t)yi * ss + xi;
    } else {
        uint8_t *tmp = d->scratch;
        edge_window(plane, ss, pw, ph, xi - 2, yi - 2, w + 5, h + 5, tmp, 24);
        src = tmp + 2 * 24 + 2;
        ss = 24;
    }
    luma_qpel(src, ss, dst, stride, w, h, fx, fy);
}

void h264_mc_chroma(H264Dec *d, const H264Pic *ref, int plane, uint8_t *dst, int stride, int x, int y, int w, int h, int mvx, int mvy)
{
    int xi = x + (mvx >> 3), yi = y + (mvy >> 3), fx = mvx & 7, fy = mvy & 7;
    int pw = d->mbw * 8, ph = d->mbh * 8, ss = d->stride[1];
    const uint8_t *src;
    if (xi >= -PAD_C && yi >= -PAD_C && xi + w + 1 <= pw + PAD_C && yi + h + 1 <= ph + PAD_C) {
        src = ref->p[plane] + (ptrdiff_t)yi * ss + xi;
    } else {
        edge_window(ref->p[plane], ss, pw, ph, xi, yi, w + 1, h + 1, d->scratch, 24);
        src = d->scratch;
        ss = 24;
    }
    if (!fx && !fy) {
        if (w == 8)
            for (int j = 0; j < h; j++)
                memcpy(dst + j * stride, src + j * ss, 8);
        else if (w == 4)
            for (int j = 0; j < h; j++)
                memcpy(dst + j * stride, src + j * ss, 4);
        else
            for (int j = 0; j < h; j++)
                memcpy(dst + j * stride, src + j * ss, 2);
        return;
    }
    if (!fy) { /* between two pixels of a row */
        int ca = 8 - fx, cb = fx;
        for (int j = 0; j < h; j++) {
            const uint8_t *s = src + j * ss;
            for (int i = 0; i < w; i++)
                dst[j * stride + i] = (uint8_t)((ca * s[i] + cb * s[i + 1] + 4) >> 3);
        }
        return;
    }
    if (!fx) {
        int ca = 8 - fy, cb = fy;
        for (int j = 0; j < h; j++) {
            const uint8_t *s = src + j * ss;
            for (int i = 0; i < w; i++)
                dst[j * stride + i] = (uint8_t)((ca * s[i] + cb * s[i + ss] + 4) >> 3);
        }
        return;
    }
    int ca = (8 - fx) * (8 - fy), cb = fx * (8 - fy), cc = (8 - fx) * fy, cd = fx * fy;
    for (int j = 0; j < h; j++) {
        const uint8_t *s = src + j * ss;
        for (int i = 0; i < w; i++)
            dst[j * stride + i] = (uint8_t)((ca * s[i] + cb * s[i + 1] + cc * s[i + ss] + cd * s[i + ss + 1] + 32) >> 6);
    }
}
