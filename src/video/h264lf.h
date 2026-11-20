/* psp5 authentic – the H.264 decoder in software: the loop filter (the standard's 8.7) and the borders of the reference pictures. */
#include <string.h>
#include <stdlib.h>

#include "h264int.h"

static const uint8_t ALPHA[52] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 5, 6, 7, 8, 9, 10, 12, 13, 15, 17, 20, 22, 25, 28, 32, 36, 40, 45, 50, 56, 63, 71, 80, 90, 101, 113, 127, 144, 162, 182, 203, 226, 255, 255 };
static const uint8_t BETA[52] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18 };
static const uint8_t TC0[52][3] = {
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
    {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 1, 1}, {0, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 2}, {1, 1, 2}, {1, 1, 2}, {1, 1, 2}, {1, 2, 3}, {1, 2, 3}, {2, 2, 3},
    {2, 2, 4}, {2, 3, 4}, {2, 3, 4}, {3, 3, 5}, {3, 4, 6}, {3, 4, 6}, {4, 5, 7}, {4, 5, 8}, {4, 6, 9}, {5, 7, 10}, {6, 8, 11}, {6, 8, 13}, {7, 10, 14}, {8, 11, 16}, {9, 12, 18}, {10, 13, 20}, {11, 15, 23}, {13, 17, 25}
};

static inline int iabs(int v) { return v < 0 ? -v : v; }
static inline int clip3(int lo, int hi, int v) { return v < lo ? lo : v > hi ? hi : v; }
static inline uint8_t clip1(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

/* The filters of one line across an edge (the standard's 8.7.2). P0 / Q0 are the addresses of the samples next to the edge, P1 .. P3 / Q1 .. Q3 the ones further away, by distance. The same text serves
 * a vertical edge (the line is a row: the addresses are q - 3 .. q + 3) and a horizontal one (the line is a column: the addresses are in six rows), so the offsets are numbers the compiler knows and no
 * address is worked out for a sample. The tests of the standard (|p0 - q0| < alpha ...) are one unsigned comparison each (a2 = 2 * alpha - 1, b2 = 2 * beta - 1). */

/* luma, bS 1 to 3 */
#define LN(P2, P1, P0, Q0, Q1, Q2) \
    do { \
        int p0 = *(P0), q0 = *(Q0); \
        if ((unsigned)(p0 - q0 + alpha - 1) < a2) { \
            int p1 = *(P1), q1 = *(Q1); \
            if ((unsigned)(p1 - p0 + beta - 1) < b2 && (unsigned)(q1 - q0 + beta - 1) < b2) { \
                int p2 = *(P2), q2 = *(Q2); \
                int ap = (unsigned)(p2 - p0 + beta - 1) < b2, aq = (unsigned)(q2 - q0 + beta - 1) < b2; \
                int tc = tc0 + ap + aq; \
                int delta = clip3(-tc, tc, (((q0 - p0) * 4) + (p1 - q1) + 4) >> 3); \
                if (tc0) { /* (with tc0 = 0 the two corrections are clipped to 0: p1 and q1 stay) */ \
                    int avg = (p0 + q0 + 1) >> 1; \
                    if (ap) \
                        *(P1) = (uint8_t)(p1 + clip3(-tc0, tc0, (p2 + avg - (p1 << 1)) >> 1)); \
                    if (aq) \
                        *(Q1) = (uint8_t)(q1 + clip3(-tc0, tc0, (q2 + avg - (q1 << 1)) >> 1)); \
                } \
                *(P0) = clip1(p0 + delta); \
                *(Q0) = clip1(q0 - delta); \
            } \
        } \
    } while (0)

/* luma, bS 4 (thr2 = 2 * ((alpha >> 2) + 2) - 1: the test |p0 - q0| < (alpha >> 2) + 2) */
#define LS(P3, P2, P1, P0, Q0, Q1, Q2, Q3) \
    do { \
        int p0 = *(P0), q0 = *(Q0); \
        if ((unsigned)(p0 - q0 + alpha - 1) < a2) { \
            int p1 = *(P1), q1 = *(Q1); \
            if ((unsigned)(p1 - p0 + beta - 1) < b2 && (unsigned)(q1 - q0 + beta - 1) < b2) { \
                int p2 = *(P2), q2 = *(Q2); \
                int small = (unsigned)(p0 - q0 + thr - 1) < thr2; \
                if (small && (unsigned)(p2 - p0 + beta - 1) < b2) { \
                    int p3 = *(P3); \
                    *(P0) = (uint8_t)((p2 + 2 * p1 + 2 * p0 + 2 * q0 + q1 + 4) >> 3); \
                    *(P1) = (uint8_t)((p2 + p1 + p0 + q0 + 2) >> 2); \
                    *(P2) = (uint8_t)((2 * p3 + 3 * p2 + p1 + p0 + q0 + 4) >> 3); \
                } else { \
                    *(P0) = (uint8_t)((2 * p1 + p0 + q1 + 2) >> 2); \
                } \
                if (small && (unsigned)(q2 - q0 + beta - 1) < b2) { \
                    int q3 = *(Q3); \
                    *(Q0) = (uint8_t)((p1 + 2 * p0 + 2 * q0 + 2 * q1 + q2 + 4) >> 3); \
                    *(Q1) = (uint8_t)((p0 + q0 + q1 + q2 + 2) >> 2); \
                    *(Q2) = (uint8_t)((2 * q3 + 3 * q2 + q1 + q0 + p0 + 4) >> 3); \
                } else { \
                    *(Q0) = (uint8_t)((2 * q1 + q0 + p1 + 2) >> 2); \
                } \
            } \
        } \
    } while (0)

/* chroma, bS 1 to 3 (tc is tc0 + 1) and bS 4 */
#define CN(P1, P0, Q0, Q1) \
    do { \
        int p0 = *(P0), q0 = *(Q0); \
        if ((unsigned)(p0 - q0 + alpha - 1) < a2) { \
            int p1 = *(P1), q1 = *(Q1); \
            if ((unsigned)(p1 - p0 + beta - 1) < b2 && (unsigned)(q1 - q0 + beta - 1) < b2) { \
                int delta = clip3(-tc, tc, (((q0 - p0) * 4) + (p1 - q1) + 4) >> 3); \
                *(P0) = clip1(p0 + delta); \
                *(Q0) = clip1(q0 - delta); \
            } \
        } \
    } while (0)
#define CS(P1, P0, Q0, Q1) \
    do { \
        int p0 = *(P0), q0 = *(Q0); \
        if ((unsigned)(p0 - q0 + alpha - 1) < a2) { \
            int p1 = *(P1), q1 = *(Q1); \
            if ((unsigned)(p1 - p0 + beta - 1) < b2 && (unsigned)(q1 - q0 + beta - 1) < b2) { \
                *(P0) = (uint8_t)((2 * p1 + p0 + q1 + 2) >> 2); \
                *(Q0) = (uint8_t)((2 * q1 + q0 + p1 + 2) >> 2); \
            } \
        } \
    } while (0)

/* a vertical edge of luma: 16 rows, q is the first sample right of the edge in the first row, st the distance of two rows; bs has the four strengths (of four rows each) */
static __attribute__((noinline)) void edge_luma_v(uint8_t *q, int st, const int8_t *bs, int alpha, int beta, int ia)
{
    unsigned a2 = (unsigned)(2 * alpha - 1), b2 = (unsigned)(2 * beta - 1);
    int thr = (alpha >> 2) + 2;
    unsigned thr2 = (unsigned)(2 * thr - 1);
    for (int s = 0; s < 4; s++, q += 4 * st) {
        int b = bs[s];
        if (!b)
            continue;
        uint8_t *r = q;
        if (b < 4) {
            int tc0 = TC0[ia][b - 1];
            for (int k = 0; k < 4; k++, r += st)
                LN(r - 3, r - 2, r - 1, r, r + 1, r + 2);
        } else {
            for (int k = 0; k < 4; k++, r += st)
                LS(r - 4, r - 3, r - 2, r - 1, r, r + 1, r + 2, r + 3);
        }
    }
}

/* a horizontal edge of luma: q is the first sample below the edge in the first column, st the distance of two rows */
static __attribute__((noinline)) void edge_luma_h(uint8_t *q, int st, const int8_t *bs, int alpha, int beta, int ia)
{
    unsigned a2 = (unsigned)(2 * alpha - 1), b2 = (unsigned)(2 * beta - 1);
    int thr = (alpha >> 2) + 2;
    unsigned thr2 = (unsigned)(2 * thr - 1);
    for (int s = 0; s < 4; s++, q += 4) {
        int b = bs[s];
        if (!b)
            continue;
        uint8_t *rm4 = q - 4 * st, *rm3 = q - 3 * st, *rm2 = q - 2 * st, *rm1 = q - st, *rp1 = q + st, *rp2 = q + 2 * st, *rp3 = q + 3 * st;
        if (b < 4) {
            int tc0 = TC0[ia][b - 1];
            LN(rm3 + 0, rm2 + 0, rm1 + 0, q + 0, rp1 + 0, rp2 + 0);
            LN(rm3 + 1, rm2 + 1, rm1 + 1, q + 1, rp1 + 1, rp2 + 1);
            LN(rm3 + 2, rm2 + 2, rm1 + 2, q + 2, rp1 + 2, rp2 + 2);
            LN(rm3 + 3, rm2 + 3, rm1 + 3, q + 3, rp1 + 3, rp2 + 3);
        } else {
            LS(rm4 + 0, rm3 + 0, rm2 + 0, rm1 + 0, q + 0, rp1 + 0, rp2 + 0, rp3 + 0);
            LS(rm4 + 1, rm3 + 1, rm2 + 1, rm1 + 1, q + 1, rp1 + 1, rp2 + 1, rp3 + 1);
            LS(rm4 + 2, rm3 + 2, rm2 + 2, rm1 + 2, q + 2, rp1 + 2, rp2 + 2, rp3 + 2);
            LS(rm4 + 3, rm3 + 3, rm2 + 3, rm1 + 3, q + 3, rp1 + 3, rp2 + 3, rp3 + 3);
        }
    }
}

/* a vertical edge of chroma: 8 rows (two for every strength) */
static __attribute__((noinline)) void edge_chroma_v(uint8_t *q, int st, const int8_t *bs, int alpha, int beta, int ia)
{
    unsigned a2 = (unsigned)(2 * alpha - 1), b2 = (unsigned)(2 * beta - 1);
    for (int s = 0; s < 4; s++, q += 2 * st) {
        int b = bs[s];
        if (!b)
            continue;
        if (b < 4) {
            int tc = TC0[ia][b - 1] + 1;
            CN(q - 2, q - 1, q, q + 1);
            CN(q + st - 2, q + st - 1, q + st, q + st + 1);
        } else {
            CS(q - 2, q - 1, q, q + 1);
            CS(q + st - 2, q + st - 1, q + st, q + st + 1);
        }
    }
}

/* a horizontal edge of chroma: 8 columns */
static __attribute__((noinline)) void edge_chroma_h(uint8_t *q, int st, const int8_t *bs, int alpha, int beta, int ia)
{
    unsigned a2 = (unsigned)(2 * alpha - 1), b2 = (unsigned)(2 * beta - 1);
    for (int s = 0; s < 4; s++, q += 2) {
        int b = bs[s];
        if (!b)
            continue;
        uint8_t *rm2 = q - 2 * st, *rm1 = q - st, *rp1 = q + st;
        if (b < 4) {
            int tc = TC0[ia][b - 1] + 1;
            CN(rm2 + 0, rm1 + 0, q + 0, rp1 + 0);
            CN(rm2 + 1, rm1 + 1, q + 1, rp1 + 1);
        } else {
            CS(rm2 + 0, rm1 + 0, q + 0, rp1 + 0);
            CS(rm2 + 1, rm1 + 1, q + 1, rp1 + 1);
        }
    }
}

/* 0 or 1 by the motion of block pb of macroblock pm and block qb of macroblock qm (both inter, neither has coefficients): different pictures, or vectors a whole pixel apart */
static inline int strength_motion(const H264Dec *d, int pm, int pb, int qm, int qb)
{
    int rp = d->refpic[pm * 4 + ((pb >> 3) << 1) + ((pb & 3) >> 1)], rq = d->refpic[qm * 4 + ((qb >> 3) << 1) + ((qb & 3) >> 1)];
    if (rp != rq)
        return 1;
    const int16_t *mp = d->mv + (pm * 16 + pb) * 2, *mq = d->mv + (qm * 16 + qb) * 2;
    return iabs(mp[0] - mq[0]) >= 4 || iabs(mp[1] - mq[1]) >= 4;
}

void h264_deblock_picture(H264Dec *d)
{
    const H264Pps *pps = d->ap;
    int mbw = d->mbw;
    uint8_t *Y = d->cur->p[0], *U = d->cur->p[1], *V = d->cur->p[2];
    int sy = d->stride[0], sc = d->stride[1];
    for (int mby = 0; mby < d->mbh; mby++) {
        for (int mbx = 0; mbx < mbw; mbx++) {
            int mb = mby * mbw + mbx;
            if (d->slice_of[mb] < 0)
                continue;
            int idc = d->dbk_idc[mb];
            if (idc == 1)
                continue;
            int intra = d->kind[mb] != KIND_INTER;
            int left = mbx > 0 && d->slice_of[mb - 1] >= 0 && (idc != 2 || d->slice_of[mb - 1] == d->slice_of[mb]);
            int top = mby > 0 && d->slice_of[mb - mbw] >= 0 && (idc != 2 || d->slice_of[mb - mbw] == d->slice_of[mb]);
            int8_t bsv[4][4], bsh[4][4];
            int any = 0;
            if (intra) { /* intra: 3 inside, 4 on the edges of the macroblock */
                for (int e = 0; e < 4; e++)
                    for (int s = 0; s < 4; s++) {
                        bsv[e][s] = (int8_t)(e ? 3 : left ? 4 : 0);
                        bsh[e][s] = (int8_t)(e ? 3 : top ? 4 : 0);
                    }
                any = 1;
            } else {
                unsigned nzm = d->nzmask[mb];
                int uni = d->uni[mb]; /* the whole macroblock has one motion: no edge inside has a difference of it */
                /* with one motion on both sides, the four pieces of a macroblock edge have the same motion strength: it is worked out once */
                int mvl = -1, mvt = -1;
                if (uni) {
                    if (left && d->kind[mb - 1] == KIND_INTER && d->uni[mb - 1])
                        mvl = strength_motion(d, mb - 1, 3, mb, 0);
                    if (top && d->kind[mb - mbw] == KIND_INTER && d->uni[mb - mbw])
                        mvt = strength_motion(d, mb - mbw, 12, mb, 0);
                }
                for (int e = 0; e < 4; e++) {
                    unsigned vm, hm;
                    if (e && !nzm && uni) { /* no coefficients and one motion: no strength inside */
                        for (int s = 0; s < 4; s++)
                            bsv[e][s] = bsh[e][s] = 0;
                        continue;
                    }
                    if (e) {
                        vm = ((nzm >> e) | (nzm >> (e - 1))) & 0x1111; /* bit 4 * s: the block or the one next to it has coefficients */
                        hm = ((nzm >> (4 * e)) | (nzm >> (4 * (e - 1)))) & 0xF;
                    } else {
                        vm = nzm & 0x1111;
                        hm = nzm & 0xF;
                    }
                    for (int s = 0; s < 4; s++) {
                        int v, h;
                        if (e == 0) {
                            if (!left)
                                v = 0;
                            else if (d->kind[mb - 1] != KIND_INTER)
                                v = 4;
                            else if ((vm >> (4 * s)) & 1 || (d->nzmask[mb - 1] >> (s * 4 + 3)) & 1)
                                v = 2;
                            else
                                v = mvl >= 0 ? mvl : strength_motion(d, mb - 1, s * 4 + 3, mb, s * 4);
                            if (!top)
                                h = 0;
                            else if (d->kind[mb - mbw] != KIND_INTER)
                                h = 4;
                            else if ((hm >> s) & 1 || (d->nzmask[mb - mbw] >> (12 + s)) & 1)
                                h = 2;
                            else
                                h = mvt >= 0 ? mvt : strength_motion(d, mb - mbw, 12 + s, mb, s);
                        } else {
                            v = (vm >> (4 * s)) & 1 ? 2 : uni ? 0 : strength_motion(d, mb, s * 4 + e - 1, mb, s * 4 + e);
                            h = (hm >> s) & 1 ? 2 : uni ? 0 : strength_motion(d, mb, (e - 1) * 4 + s, mb, e * 4 + s);
                        }
                        bsv[e][s] = (int8_t)v;
                        bsh[e][s] = (int8_t)h;
                        any |= v | h;
                    }
                }
            }
            if (!any)
                continue;
            int qpq = d->qp[mb];
            int a_off = d->dbk_a[mb] * 2, b_off = d->dbk_b[mb] * 2;
            uint8_t *y = Y + (ptrdiff_t)mby * 16 * sy + mbx * 16;
            uint8_t *u = U + (ptrdiff_t)mby * 8 * sc + mbx * 8, *v = V + (ptrdiff_t)mby * 8 * sc + mbx * 8;
            /* vertical edges, then horizontal ones; luma, then chroma */
            for (int dir = 0; dir < 2; dir++) {
                for (int e = 0; e < 4; e++) {
                    const int8_t *bs = dir ? bsh[e] : bsv[e];
                    if (!(bs[0] | bs[1] | bs[2] | bs[3]))
                        continue;
                    int nb = dir ? mb - mbw : mb - 1;
                    int qpp = e == 0 ? d->qp[nb] : qpq;
                    int qav = (qpp + qpq + 1) >> 1;
                    int ia = clip3(0, 51, qav + a_off), ib = clip3(0, 51, qav + b_off);
                    int alpha = ALPHA[ia], beta = BETA[ib];
                    if (!alpha || !beta)
                        continue;
                    uint8_t *q = y + (dir ? (ptrdiff_t)e * 4 * sy : e * 4);
                    if (dir)
                        edge_luma_h(q, sy, bs, alpha, beta, ia);
                    else
                        edge_luma_v(q, sy, bs, alpha, beta, ia);
                    if (e == 0 || e == 2) {
                        int ce = e >> 1; /* chroma has two edges, at 0 and 4 */
                        int cqp_q = h264_chroma_qp[clip3(0, 51, qpq + pps->cqp_off)];
                        int cqp_p = e == 0 ? h264_chroma_qp[clip3(0, 51, qpp + pps->cqp_off)] : cqp_q;
                        int cav = (cqp_p + cqp_q + 1) >> 1;
                        int cia = clip3(0, 51, cav + a_off), cib = clip3(0, 51, cav + b_off);
                        int ca = ALPHA[cia], cb = BETA[cib];
                        if (ca && cb) {
                            ptrdiff_t off = dir ? (ptrdiff_t)ce * 4 * sc : ce * 4;
                            if (dir) {
                                edge_chroma_h(u + off, sc, bs, ca, cb, cia);
                                edge_chroma_h(v + off, sc, bs, ca, cb, cia);
                            } else {
                                edge_chroma_v(u + off, sc, bs, ca, cb, cia);
                                edge_chroma_v(v + off, sc, bs, ca, cb, cia);
                            }
                        }
                    }
                }
            }
        }
    }
}

/* the borders of a finished picture: its edge pixels copied outwards, so that the motion compensation can read beyond the picture */
void h264_extend_borders(H264Dec *d, H264Pic *p)
{
    for (int pl = 0; pl < 3; pl++) {
        int w = d->mbw * (pl ? 8 : 16), h = d->mbh * (pl ? 8 : 16), pad = pl ? PAD_C : PAD_Y, st = d->stride[pl];
        uint8_t *base = p->p[pl];
        for (int y = 0; y < h; y++) {
            uint8_t *row = base + (ptrdiff_t)y * st;
            memset(row - pad, row[0], (size_t)pad);
            memset(row + w, row[w - 1], (size_t)pad);
        }
        for (int i = 1; i <= pad; i++) {
            memcpy(base - (ptrdiff_t)i * st - pad, base - pad, (size_t)(w + 2 * pad));
            memcpy(base + (ptrdiff_t)(h - 1 + i) * st - pad, base + (ptrdiff_t)(h - 1) * st - pad, (size_t)(w + 2 * pad));
        }
    }
}
