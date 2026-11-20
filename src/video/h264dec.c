/* psp5 authentic – the H.264 decoder in software (see h264dec.h): the stream (parameter sets, slice headers, reference pictures) and the macroblock layer of CAVLC. Written from the standard
 * (ITU-T H.264, clauses 7, 8 and 9.1 / 9.2). */
#include "h264dec.h"
#include "h264int.h"

#include <stdlib.h>
#include <string.h>

#include "h264tab.inc"

/* ---------------------------------------------------------------------- */
/* bits */

typedef struct {
    const uint8_t *p; /* with 8 bytes of zeros after the end */
    uint32_t pos, end; /* in bits */
} Br;

static inline uint32_t peek32(const Br *b)
{
    if (b->pos > b->end + 64)
        return 0;
    const uint8_t *q = b->p + (b->pos >> 3);
    unsigned sh = b->pos & 7;
    uint32_t v = ((uint32_t)q[0] << 24) | ((uint32_t)q[1] << 16) | ((uint32_t)q[2] << 8) | q[3];
    if (sh)
        v = (v << sh) | (q[4] >> (8 - sh));
    return v;
}

static inline uint32_t getn(Br *b, int n) /* n in 1..24 */
{
    uint32_t v = peek32(b) >> (32 - n);
    b->pos += (uint32_t)n;
    return v;
}

static inline int get1(Br *b)
{
    if (b->pos > b->end + 64) {
        b->pos++;
        return 0;
    }
    int v = (b->p[b->pos >> 3] >> (7 - (b->pos & 7))) & 1;
    b->pos++;
    return v;
}

static uint32_t get_ue(Br *b)
{
    uint32_t v = peek32(b);
    if (!v) { /* 32 zeros or more: not a number of ours */
        b->pos += 32;
        return 0xFFFFFFu;
    }
    int lz = __builtin_clz(v);
    if (lz <= 15) {
        b->pos += (uint32_t)(2 * lz + 1);
        return (v >> (31 - 2 * lz)) - 1;
    }
    b->pos += (uint32_t)lz + 1;
    uint32_t r = 0;
    for (int i = 0; i < lz; i++)
        r = (r << 1) | (uint32_t)get1(b);
    return (1u << lz) - 1 + r;
}

static int get_se(Br *b)
{
    uint32_t k = get_ue(b);
    return (k & 1) ? (int)((k + 1) >> 1) : -(int)(k >> 1);
}

static int more_data(const Br *b, uint32_t stop) { return b->pos < stop; }

/* ---------------------------------------------------------------------- */
/* the VLC tables of CAVLC */

typedef struct {
    uint8_t lut_len[256], lut_sym[256]; /* by the first 8 bits: the code, when it is that short */
    int nslow;
    struct { uint16_t code; uint8_t len, sym; } slow[72];
} Vlc;

static Vlc g_ct[5], g_tz[16], g_tzc[4], g_rb[8];
static int g_vlc_ready;

static void vlc_add(Vlc *v, int code, int len, int sym)
{
    if (!len)
        return;
    if (len <= 8) {
        int lo = code << (8 - len);
        for (int i = 0; i < (1 << (8 - len)); i++) {
            v->lut_len[lo + i] = (uint8_t)len;
            v->lut_sym[lo + i] = (uint8_t)sym;
        }
    } else {
        v->slow[v->nslow].code = (uint16_t)code;
        v->slow[v->nslow].len = (uint8_t)len;
        v->slow[v->nslow].sym = (uint8_t)sym;
        v->nslow++;
    }
}

static void vlc_init(void)
{
    if (g_vlc_ready)
        return;
    for (int n = 0; n < 5; n++)
        for (int tc = 0; tc < 17; tc++)
            for (int t1 = 0; t1 < 4; t1++)
                vlc_add(&g_ct[n], h264_ct_code[n][tc][t1], h264_ct_len[n][tc][t1], tc * 4 + t1);
    for (int i = 1; i < 16; i++)
        for (int z = 0; z < 16; z++)
            vlc_add(&g_tz[i], h264_tz_code[i][z], h264_tz_len[i][z], z);
    for (int i = 1; i < 4; i++)
        for (int z = 0; z < 4; z++)
            vlc_add(&g_tzc[i], h264_tzc_code[i][z], h264_tzc_len[i][z], z);
    for (int i = 1; i < 8; i++)
        for (int r = 0; r < 15; r++)
            vlc_add(&g_rb[i], h264_rb_code[i][r], h264_rb_len[i][r], r);
    g_vlc_ready = 1;
}

static inline int vlc_get(const Vlc *v, Br *b)
{
    uint32_t w = peek32(b);
    unsigned i = w >> 24;
    if (v->lut_len[i]) {
        b->pos += v->lut_len[i];
        return v->lut_sym[i];
    }
    uint32_t w16 = w >> 16;
    for (int k = 0; k < v->nslow; k++)
        if ((w16 >> (16 - v->slow[k].len)) == v->slow[k].code) {
            b->pos += v->slow[k].len;
            return v->slow[k].sym;
        }
    b->pos = b->end + 65; /* not a code */
    return 0;
}

/* ---------------------------------------------------------------------- */
/* tables of the macroblock layer */

static const uint8_t ZIGZAG[16] = { 0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15 }; /* scan index -> raster position (y * 4 + x) */
static const uint8_t CBP_INTRA[48] = { 47, 31, 15, 0, 23, 27, 29, 30, 7, 11, 13, 14, 39, 43, 45, 46, 16, 3, 5, 10, 12, 19, 21, 26, 28, 35, 37, 42, 44, 1, 2, 4, 8, 17, 18, 20, 24, 6, 9, 22, 25, 32, 33, 34, 36, 40, 38, 41 };
static const uint8_t CBP_INTER[48] = { 0, 16, 1, 2, 4, 8, 32, 3, 5, 10, 12, 15, 47, 7, 11, 13, 14, 6, 9, 31, 35, 37, 42, 44, 33, 34, 36, 40, 39, 43, 45, 46, 17, 18, 20, 24, 19, 21, 26, 28, 23, 27, 29, 30, 22, 25, 38, 41 };
static const uint8_t BLK_X[16] = { 0, 1, 0, 1, 2, 3, 2, 3, 0, 1, 0, 1, 2, 3, 2, 3 }; /* block number (the order of the stream) -> its place in 4x4 blocks */
static const uint8_t BLK_Y[16] = { 0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 3, 3, 2, 2, 3, 3 };

static uint16_t g_dq[6][16]; /* the scaling of a coefficient by position (flat matrices): the standard's normAdjust */
static int g_dq_ready;

static void dq_init(void)
{
    static const uint8_t V[6][3] = { { 10, 16, 13 }, { 11, 18, 14 }, { 13, 20, 16 }, { 14, 23, 18 }, { 16, 25, 20 }, { 18, 29, 23 } };
    if (g_dq_ready)
        return;
    for (int m = 0; m < 6; m++)
        for (int p = 0; p < 16; p++) {
            int r = p >> 2, c = p & 3;
            g_dq[m][p] = V[m][(r % 2 == 0 && c % 2 == 0) ? 0 : (r % 2 == 1 && c % 2 == 1) ? 1 : 2];
        }
    g_dq_ready = 1;
}

/* ---------------------------------------------------------------------- */
/* NAL units, parameter sets, slice headers */

/* the NAL unit without its emulation prevention bytes (00 00 03), with zeros after it; returns the length of the payload (without the header byte) */
static int unescape(H264Dec *d, const uint8_t *nal, int len)
{
    if (len + 16 > d->rbsp_cap) {
        uint8_t *n = realloc(d->rbsp, (size_t)len + 16 + 4096);
        if (!n)
            return -1;
        d->rbsp = n;
        d->rbsp_cap = len + 16 + 4096;
    }
    uint8_t *o = d->rbsp;
    int n = 0, zeros = 0;
    for (int i = 1; i < len; i++) {
        uint8_t c = nal[i];
        if (zeros >= 2 && c == 3) {
            zeros = 0;
            continue;
        }
        zeros = c == 0 ? zeros + 1 : 0;
        o[n++] = c;
    }
    memset(o + n, 0, 16);
    return n;
}

/* the bit position of the stop bit that ends the payload */
static uint32_t find_stop_bit(const uint8_t *p, int n)
{
    int i = n - 1;
    while (i >= 0 && !p[i])
        i--;
    if (i < 0)
        return 0;
    int bit = 0;
    while (!((p[i] >> bit) & 1))
        bit++;
    return (uint32_t)i * 8 + (uint32_t)(7 - bit);
}

static int parse_sps(H264Dec *d, Br *b)
{
    H264Sps s;
    memset(&s, 0, sizeof(s));
    s.profile = (int)getn(b, 8);
    getn(b, 8);
    s.level = (int)getn(b, 8);
    s.id = (int)get_ue(b);
    if (s.id > 7)
        return 0;
    if (s.profile == 100 || s.profile == 110 || s.profile == 122 || s.profile == 244 || s.profile == 44 || s.profile == 83 || s.profile == 86 || s.profile == 118 || s.profile == 128) {
        int cf = (int)get_ue(b);
        if (cf != 1) {
            d->why = H264_FORMAT;
            return 0;
        }
        int bl = (int)get_ue(b), bc = (int)get_ue(b);
        get1(b); /* qpprime_y_zero_transform_bypass_flag */
        if (bl || bc) {
            d->why = H264_FORMAT;
            return 0;
        }
        if (get1(b)) { /* scaling matrices */
            d->why = H264_HIGH_TOOLS;
            return 0;
        }
    }
    s.log2_max_fn = (int)get_ue(b) + 4;
    s.poc_type = (int)get_ue(b);
    if (s.log2_max_fn > 16)
        return 0;
    if (s.poc_type == 0) {
        s.log2_max_poc = (int)get_ue(b) + 4;
    } else if (s.poc_type == 1) {
        s.always_zero = get1(b);
        get_se(b);
        get_se(b);
        int n = (int)get_ue(b);
        if (n > 255)
            return 0;
        for (int i = 0; i < n; i++)
            get_se(b);
    }
    s.nref = (int)get_ue(b);
    get1(b); /* gaps_in_frame_num_value_allowed_flag */
    s.mbw = (int)get_ue(b) + 1;
    s.mbh = (int)get_ue(b) + 1;
    s.frame_mbs_only = get1(b);
    if (!s.frame_mbs_only) {
        d->why = H264_INTERLACED;
        return 0;
    }
    s.direct8x8 = get1(b);
    if (get1(b)) {
        s.crop_l = (int)get_ue(b);
        s.crop_r = (int)get_ue(b);
        s.crop_t = (int)get_ue(b);
        s.crop_b = (int)get_ue(b);
    }
    if (s.nref > H264_MAXREF || s.mbw > 64 || s.mbh > 64 || s.mbw * s.mbh > 2048 || b->pos > b->end) {
        d->why = H264_SIZE;
        return 0;
    }
    if (16 * s.mbw - 2 * (s.crop_l + s.crop_r) <= 0 || 16 * s.mbh - 2 * (s.crop_t + s.crop_b) <= 0)
        return 0;
    s.valid = 1;
    d->sps[s.id] = s;
    return 1;
}

static int parse_pps(H264Dec *d, Br *b, uint32_t stop)
{
    H264Pps p;
    memset(&p, 0, sizeof(p));
    p.id = (int)get_ue(b);
    p.sps_id = (int)get_ue(b);
    if (p.id > 15 || p.sps_id > 7)
        return 0;
    p.cabac = get1(b);
    p.bottom_poc = get1(b);
    p.nslice_groups = (int)get_ue(b) + 1;
    if (p.nslice_groups > 1) {
        d->why = H264_FMO;
        return 0;
    }
    p.nref[0] = (int)get_ue(b) + 1;
    p.nref[1] = (int)get_ue(b) + 1;
    p.weighted = get1(b);
    p.weighted_bi = (int)getn(b, 2);
    p.qp = 26 + get_se(b);
    p.qs = 26 + get_se(b);
    p.cqp_off = get_se(b);
    p.dbk_ctrl = get1(b);
    p.constrained = get1(b);
    p.redundant = get1(b);
    if (more_data(b, stop)) { /* the High profiles' tail */
        if (get1(b)) {
            d->why = H264_HIGH_TOOLS; /* transform_8x8_mode_flag */
            return 0;
        }
        if (get1(b)) {
            d->why = H264_HIGH_TOOLS;
            return 0;
        }
        if (get_se(b) != p.cqp_off) { /* second_chroma_qp_index_offset: only the same one for both is taken */
            d->why = H264_HIGH_TOOLS;
            return 0;
        }
    }
    if (p.nref[0] > H264_MAXREF || b->pos > b->end)
        return 0;
    p.valid = 1;
    d->pps[p.id] = p;
    return 1;
}

/* ---------------------------------------------------------------------- */
/* memory of the active parameter set */

static void free_pictures(H264Dec *d)
{
    for (int i = 0; i < d->ndpb; i++)
        free(d->dpb[i].buf);
    free(d->dpb);
    d->dpb = NULL;
    d->ndpb = 0;
    d->cur = d->out = NULL;
    free(d->slice_of); free(d->kind); free(d->qp); free(d->dbk_idc); free(d->uni); free(d->pdirty); free(d->acc[0]); free(d->acc[1]); free(d->acc[2]); free(d->dbk_a); free(d->dbk_b);
    free(d->ipred); free(d->nz); free(d->nzmask); free(d->mv); free(d->refi); free(d->refpic);
    d->slice_of = NULL; d->kind = d->qp = d->dbk_idc = d->uni = d->pdirty = NULL;
    d->acc[0] = d->acc[1] = d->acc[2] = NULL; d->dbk_a = d->dbk_b = d->ipred = NULL;
    d->nz = NULL; d->nzmask = NULL; d->mv = NULL; d->refi = NULL; d->refpic = NULL;
    d->as = NULL;
}

static int activate(H264Dec *d, const H264Sps *s)
{
    if (d->as && d->mbw == s->mbw && d->mbh == s->mbh && d->ndpb >= s->nref + 2) {
        d->as = s;
        return 1;
    }
    free_pictures(d);
    d->mbw = s->mbw;
    d->mbh = s->mbh;
    d->nmb = s->mbw * s->mbh;
    d->stride[0] = s->mbw * 16 + 2 * PAD_Y;
    d->stride[1] = d->stride[2] = (s->mbw * 8 + 2 * PAD_C + 15) & ~15;
    d->plane_size[0] = d->stride[0] * (s->mbh * 16 + 2 * PAD_Y);
    d->plane_size[1] = d->plane_size[2] = d->stride[1] * (s->mbh * 8 + 2 * PAD_C);
    d->pic_bytes = d->plane_size[0] + d->plane_size[1] * 2;
    d->width = 16 * s->mbw - 2 * (s->crop_l + s->crop_r);
    d->height = 16 * s->mbh - 2 * (s->crop_t + s->crop_b);
    d->ndpb = (s->nref < 1 ? 1 : s->nref) + 2;
    d->dpb = calloc((size_t)d->ndpb, sizeof(H264Pic));
    int n = d->nmb;
    d->slice_of = malloc(sizeof(int16_t) * (size_t)n);
    d->kind = calloc((size_t)n, 1);
    d->qp = calloc((size_t)n, 1);
    d->dbk_idc = calloc((size_t)n, 1);
    d->uni = calloc((size_t)n, 1);
    d->pdirty = calloc((size_t)n, 1);
    for (int k = 0; k < 3; k++)
        d->acc[k] = calloc((size_t)n, 1);
    d->dbk_a = calloc((size_t)n, 1);
    d->dbk_b = calloc((size_t)n, 1);
    d->ipred = calloc((size_t)n * 16, 1);
    d->nz = calloc((size_t)n * 24, 1);
    d->nzmask = calloc((size_t)n, sizeof(uint16_t));
    d->mv = calloc((size_t)n * 32, sizeof(int16_t));
    d->refi = calloc((size_t)n * 4, 1);
    d->refpic = calloc((size_t)n * 4, sizeof(int));
    int ok = d->dpb && d->slice_of && d->kind && d->qp && d->dbk_idc && d->uni && d->pdirty && d->acc[0] && d->acc[1] && d->acc[2] && d->dbk_a && d->dbk_b && d->ipred && d->nz && d->nzmask && d->mv && d->refi && d->refpic;
    for (int i = 0; ok && i < d->ndpb; i++) {
        d->dpb[i].buf = malloc((size_t)d->pic_bytes);
        if (!d->dpb[i].buf) {
            ok = 0;
            break;
        }
        memset(d->dpb[i].buf, 128, (size_t)d->pic_bytes);
        d->dpb[i].p[0] = d->dpb[i].buf + PAD_Y * d->stride[0] + PAD_Y;
        d->dpb[i].p[1] = d->dpb[i].buf + d->plane_size[0] + PAD_C * d->stride[1] + PAD_C;
        d->dpb[i].p[2] = d->dpb[i].buf + d->plane_size[0] + d->plane_size[1] + PAD_C * d->stride[1] + PAD_C;
    }
    if (!ok) {
        free_pictures(d);
        d->why = H264_MEMORY;
        return 0;
    }
    d->as = s;
    d->have_idr = 0;
    d->dirty_all[0] = d->dirty_all[1] = d->dirty_all[2] = 1;
    return 1;
}

/* ---------------------------------------------------------------------- */
/* reference pictures */

static void mark_all_unused(H264Dec *d)
{
    for (int i = 0; i < d->ndpb; i++)
        d->dpb[i].ref = 0;
}

static int cmp_wrap_desc(const void *a, const void *b)
{
    const H264Pic *x = *(H264Pic *const *)a, *y = *(H264Pic *const *)b;
    return y->wrap - x->wrap;
}

static int cmp_long_asc(const void *a, const void *b)
{
    const H264Pic *x = *(H264Pic *const *)a, *y = *(H264Pic *const *)b;
    return x->long_idx - y->long_idx;
}

/* the reference list of a P slice: the short term pictures by descending picture number, then the long term ones; then the changes the slice header asks for */
static int build_ref_list(H264Dec *d)
{
    H264Pic *sh[H264_MAXREF + 2], *lg[H264_MAXREF + 2];
    int ns = 0, nl = 0;
    for (int i = 0; i < d->ndpb; i++) {
        H264Pic *p = &d->dpb[i];
        if (p == d->cur)
            continue;
        if (p->ref == 1) {
            p->wrap = p->frame_num > d->sl.frame_num ? p->frame_num - d->max_fn : p->frame_num;
            sh[ns++] = p;
        } else if (p->ref == 2) {
            lg[nl++] = p;
        }
    }
    qsort(sh, (size_t)ns, sizeof(sh[0]), cmp_wrap_desc);
    qsort(lg, (size_t)nl, sizeof(lg[0]), cmp_long_asc);
    int n = d->sl.nref[0];
    H264Pic *list[H264_MAXREF + 2];
    int cnt = 0;
    for (int i = 0; i < ns && cnt < H264_MAXREF; i++)
        list[cnt++] = sh[i];
    for (int i = 0; i < nl && cnt < H264_MAXREF; i++)
        list[cnt++] = lg[i];
    if (cnt == 0)
        return 0;
    for (int i = cnt; i < H264_MAXREF + 1; i++)
        list[i] = NULL;
    /* the changes */
    int idx = 0, pred = d->sl.frame_num;
    for (int k = 0; k < d->sl.reorder_n && idx < n; k++) {
        int idc = d->sl.reorder[k].idc, val = d->sl.reorder[k].val;
        H264Pic *pic = NULL;
        if (idc == 0 || idc == 1) {
            if (idc == 0) {
                pred -= val + 1;
                if (pred < 0)
                    pred += d->max_fn;
            } else {
                pred += val + 1;
                if (pred >= d->max_fn)
                    pred -= d->max_fn;
            }
            int pn = pred > d->sl.frame_num ? pred - d->max_fn : pred;
            for (int i = 0; i < ns; i++)
                if (sh[i]->wrap == pn)
                    pic = sh[i];
        } else if (idc == 2) {
            for (int i = 0; i < nl; i++)
                if (lg[i]->long_idx == val)
                    pic = lg[i];
        }
        if (!pic)
            continue;
        for (int i = H264_MAXREF; i > idx; i--)
            list[i] = list[i - 1];
        list[idx++] = pic;
        int w = idx;
        for (int i = idx; i <= H264_MAXREF; i++)
            if (list[i] != pic)
                list[w++] = list[i];
        for (int i = w; i <= H264_MAXREF; i++)
            list[i] = NULL;
    }
    d->nrl = n;
    H264Pic *last = list[0];
    for (int i = 0; i < n; i++) {
        if (list[i])
            last = list[i];
        d->rl[i] = list[i] ? list[i] : last; /* a place without a picture gets the nearest one before it */
    }
    return 1;
}

/* what is done with the reference pictures when the current one is decoded (8.2.5) */
static void mark_references(H264Dec *d, const H264Slice *sl)
{
    H264Pic *cur = d->cur;
    int cur_long = 0;
    if (!sl->nal_ref_idc) {
        cur->ref = 0;
        return;
    }
    cur->frame_num = sl->frame_num;
    if (sl->idr) {
        mark_all_unused(d);
        if (sl->long_term_ref) {
            cur->ref = 2;
            cur->long_idx = 0;
            return;
        }
    } else if (sl->adaptive) {
        for (int i = 0; i < d->ndpb; i++) { /* picture numbers relative to the current frame */
            H264Pic *p = &d->dpb[i];
            if (p->ref == 1)
                p->wrap = p->frame_num > sl->frame_num ? p->frame_num - d->max_fn : p->frame_num;
        }
        for (int k = 0; k < sl->mmco_n; k++) {
            int op = sl->mmco[k].op, a = sl->mmco[k].a, b = sl->mmco[k].b;
            if (op == 1 || op == 3) {
                int pn = sl->frame_num - (a + 1);
                for (int i = 0; i < d->ndpb; i++) {
                    H264Pic *p = &d->dpb[i];
                    if (p != cur && p->ref == 1 && p->wrap == pn) {
                        if (op == 1) {
                            p->ref = 0;
                        } else {
                            for (int j = 0; j < d->ndpb; j++)
                                if (d->dpb[j].ref == 2 && d->dpb[j].long_idx == b)
                                    d->dpb[j].ref = 0;
                            p->ref = 2;
                            p->long_idx = b;
                        }
                    }
                }
            } else if (op == 2) {
                for (int i = 0; i < d->ndpb; i++)
                    if (d->dpb[i].ref == 2 && d->dpb[i].long_idx == a)
                        d->dpb[i].ref = 0;
            } else if (op == 4) {
                for (int i = 0; i < d->ndpb; i++)
                    if (d->dpb[i].ref == 2 && d->dpb[i].long_idx >= a - 1)
                        d->dpb[i].ref = 0;
            } else if (op == 5) {
                mark_all_unused(d);
            } else if (op == 6) {
                for (int j = 0; j < d->ndpb; j++)
                    if (d->dpb[j].ref == 2 && d->dpb[j].long_idx == a)
                        d->dpb[j].ref = 0;
                cur->ref = 2;
                cur->long_idx = a;
                cur_long = 1;
            }
        }
    } else { /* the sliding window */
        int ns = 0, nl = 0;
        H264Pic *oldest = NULL;
        for (int i = 0; i < d->ndpb; i++) {
            H264Pic *p = &d->dpb[i];
            if (p == cur)
                continue;
            if (p->ref == 1) {
                p->wrap = p->frame_num > sl->frame_num ? p->frame_num - d->max_fn : p->frame_num;
                ns++;
                if (!oldest || p->wrap < oldest->wrap)
                    oldest = p;
            } else if (p->ref == 2) {
                nl++;
            }
        }
        int max = d->as->nref < 1 ? 1 : d->as->nref;
        if (ns + nl >= max && oldest)
            oldest->ref = 0;
    }
    if (!cur_long)
        cur->ref = 1;
}

/* ---------------------------------------------------------------------- */
/* the macroblock layer */

static inline int mb_avail(const H264Dec *d, int mb) { return d->slice_of[mb] == d->cur_slice; }

/* the number of coefficients of the neighbours of block (bx, by) of the macroblock, for the choice of the table: -1 when the neighbour is not there */
static inline int nz_left(const H264Dec *d, int bx, int by)
{
    if (bx > 0)
        return d->nz[d->mb * 24 + by * 4 + bx - 1];
    if (d->mbx > 0 && mb_avail(d, d->mb - 1))
        return d->nz[(d->mb - 1) * 24 + by * 4 + 3];
    return -1;
}

static inline int nz_top(const H264Dec *d, int bx, int by)
{
    if (by > 0)
        return d->nz[d->mb * 24 + (by - 1) * 4 + bx];
    if (d->mby > 0 && mb_avail(d, d->mb - d->mbw))
        return d->nz[(d->mb - d->mbw) * 24 + 12 + bx];
    return -1;
}

static inline int nz_left_c(const H264Dec *d, int pl, int bx, int by)
{
    if (bx > 0)
        return d->nz[d->mb * 24 + 16 + pl * 4 + by * 2];
    if (d->mbx > 0 && mb_avail(d, d->mb - 1))
        return d->nz[(d->mb - 1) * 24 + 16 + pl * 4 + by * 2 + 1];
    return -1;
}

static inline int nz_top_c(const H264Dec *d, int pl, int bx, int by)
{
    if (by > 0)
        return d->nz[d->mb * 24 + 16 + pl * 4 + bx];
    if (d->mby > 0 && mb_avail(d, d->mb - d->mbw))
        return d->nz[(d->mb - d->mbw) * 24 + 16 + pl * 4 + 2 + bx];
    return -1;
}

static inline int nc_of(int a, int b)
{
    if (a >= 0 && b >= 0)
        return (a + b + 1) >> 1;
    return a >= 0 ? a : b >= 0 ? b : 0;
}

/* one block of coefficients: its levels in lv[] in scan order (index 0 = the first coefficient of the block, even for the AC blocks); returns the number of non-zero ones, -1 when damaged */
static int residual_block(Br *b, int nc, int max_coeff, int *lv)
{
    const Vlc *t = nc == -1 ? &g_ct[4] : nc < 2 ? &g_ct[0] : nc < 4 ? &g_ct[1] : nc < 8 ? &g_ct[2] : &g_ct[3];
    for (int i = 0; i < max_coeff; i++)
        lv[i] = 0;
    int sym = vlc_get(t, b), total = sym >> 2, t1 = sym & 3;
    if (total > max_coeff || b->pos > b->end)
        return -1;
    if (!total)
        return 0;
    int level[16], run[16];
    int suffix_len = (total > 10 && t1 < 3) ? 1 : 0;
    if (t1) { /* the signs of the trailing ones, in one read */
        uint32_t bits = peek32(b) >> (32 - t1);
        b->pos += (uint32_t)t1;
        for (int i = 0; i < t1; i++)
            level[i] = 1 - 2 * (int)((bits >> (t1 - 1 - i)) & 1);
    }
    for (int i = t1; i < total; i++) {
        uint32_t w = peek32(b);
        if (!w || b->pos > b->end)
            return -1;
        int prefix = __builtin_clz(w); /* level_prefix: the zeros before the first 1 */
        b->pos += (uint32_t)prefix + 1;
        if (prefix > 25)
            return -1;
        int code = (prefix < 15 ? prefix : 15) << suffix_len;
        if (suffix_len > 0 || prefix >= 14) {
            int size = (prefix == 14 && suffix_len == 0) ? 4 : prefix >= 15 ? prefix - 3 : suffix_len;
            if (size > 0)
                code += (int)getn(b, size);
        }
        if (prefix >= 15 && suffix_len == 0)
            code += 15;
        if (prefix >= 16)
            code += (1 << (prefix - 3)) - 4096;
        if (i == t1 && t1 < 3)
            code += 2;
        int lvl = (code & 1) ? (-code - 1) >> 1 : (code + 2) >> 1;
        level[i] = lvl;
        if (!suffix_len)
            suffix_len = 1;
        if ((lvl < 0 ? -lvl : lvl) > (3 << (suffix_len - 1)) && suffix_len < 6)
            suffix_len++;
    }
    int zeros = 0;
    if (total < max_coeff) {
        if (max_coeff == 4)
            zeros = vlc_get(&g_tzc[total], b);
        else
            zeros = vlc_get(&g_tz[total], b);
        if (zeros + total > max_coeff)
            return -1;
    }
    int left = zeros;
    for (int i = 0; i < total - 1; i++) {
        if (left > 0) {
            run[i] = vlc_get(&g_rb[left > 6 ? 7 : left], b);
            if (run[i] > left)
                return -1;
            left -= run[i];
        } else {
            run[i] = 0;
        }
    }
    run[total - 1] = left;
    int pos = -1;
    for (int i = total - 1; i >= 0; i--) {
        pos += run[i] + 1;
        if (pos >= max_coeff)
            return -1;
        lv[pos] = level[i];
    }
    return total;
}

static int clip_qp(int q) { return q < 0 ? 0 : q > 51 ? 51 : q; }

/* ----- neighbours of the motion ----- */

/* the reference index (>= 0), -1 (intra, no motion), or -2 (not there) of the block next to the macroblock's block grid at (bx, by) (coordinates -1 .. 4), and its vector */
static int nb_motion(const H264Dec *d, int bx, int by, int *mx, int *my)
{
    int nx = d->mbx, ny = d->mby;
    *mx = *my = 0;
    if (bx < 0) { nx--; bx += 4; }
    else if (bx > 3) { nx++; bx -= 4; }
    if (by < 0) { ny--; by += 4; }
    if (nx < 0 || ny < 0 || nx >= d->mbw)
        return -2;
    int a = ny * d->mbw + nx;
    if (a == d->mb) {
        if (!((d->cur_done >> (by * 4 + bx)) & 1))
            return -2;
    } else if (!mb_avail(d, a)) {
        return -2;
    }
    if (d->kind[a] != KIND_INTER)
        return -1;
    *mx = d->mv[(a * 16 + by * 4 + bx) * 2];
    *my = d->mv[(a * 16 + by * 4 + bx) * 2 + 1];
    return d->refi[a * 4 + (by >> 1) * 2 + (bx >> 1)];
}

static inline int median3(int a, int b, int c)
{
    int mx = a > b ? a : b, mn = a > b ? b : a;
    return c > mx ? mx : c < mn ? mn : c;
}

/* the predicted vector for the partition at (bx, by) of w x h blocks with reference ref; shape: 0 = any, 1 = the halves of a 16x8, 2 = the halves of a 8x16 (part: which one) */
static void predict_mv(const H264Dec *d, int bx, int by, int w, int ref, int shape, int part, int *px, int *py)
{
    int ax, ay, bxv, byv, cx, cy;
    int ra = nb_motion(d, bx - 1, by, &ax, &ay);
    int rb = nb_motion(d, bx, by - 1, &bxv, &byv);
    int rc = nb_motion(d, bx + w, by - 1, &cx, &cy);
    if (rc == -2)
        rc = nb_motion(d, bx - 1, by - 1, &cx, &cy);
    if (shape == 1) {
        if (!part && rb == ref) { *px = bxv; *py = byv; return; }
        if (part && ra == ref) { *px = ax; *py = ay; return; }
    } else if (shape == 2) {
        if (!part && ra == ref) { *px = ax; *py = ay; return; }
        if (part && rc == ref) { *px = cx; *py = cy; return; }
    }
    int m = (ra == ref) + (rb == ref) + (rc == ref);
    if (m == 1) {
        if (ra == ref) { *px = ax; *py = ay; }
        else if (rb == ref) { *px = bxv; *py = byv; }
        else { *px = cx; *py = cy; }
        return;
    }
    if (m == 0 && rb == -2 && rc == -2 && ra != -2) {
        *px = ax;
        *py = ay;
        return;
    }
    *px = median3(ax, bxv, cx);
    *py = median3(ay, byv, cy);
}

/* set the motion of a partition */
static void set_motion(H264Dec *d, int bx, int by, int w, int h, int ref, int mx, int my)
{
    int16_t *mv = d->mv + d->mb * 32;
    for (int y = by; y < by + h; y++)
        for (int x = bx; x < bx + w; x++) {
            mv[(y * 4 + x) * 2] = (int16_t)mx;
            mv[(y * 4 + x) * 2 + 1] = (int16_t)my;
            d->cur_done |= 1u << (y * 4 + x);
        }
    (void)ref;
}

/* ----- prediction of the pixels of an inter partition ----- */

static void predict_inter(H264Dec *d, int bx, int by, int w, int h, int ref, int mx, int my)
{
    const H264Pic *rp = d->rl[ref];
    int x = d->mbx * 16 + bx * 4, y = d->mby * 16 + by * 4;
    h264_mc_luma(d, rp, d->cur->p[0] + (ptrdiff_t)y * d->stride[0] + x, d->stride[0], x, y, w * 4, h * 4, mx, my);
    int cx = d->mbx * 8 + bx * 2, cy = d->mby * 8 + by * 2;
    for (int pl = 1; pl < 3; pl++)
        h264_mc_chroma(d, rp, pl, d->cur->p[pl] + (ptrdiff_t)cy * d->stride[1] + cx, d->stride[1], cx, cy, w * 2, h * 2, mx, my);
}

/* ----- intra availability ----- */

static inline int intra_ok(const H264Dec *d, int mb)
{
    return mb_avail(d, mb) && (!d->ap->constrained || d->kind[mb] != KIND_INTER);
}

/* reads the levels of a luma 4x4 block of an inter or intra 4x4 macroblock, or an AC block of an intra 16x16 one; 0 when damaged */
static int read_luma_block(H264Dec *d, Br *b, int blk, int ac_only, int qp)
{
    int bx = BLK_X[blk], by = BLK_Y[blk], lv[16];
    int nc = nc_of(nz_left(d, bx, by), nz_top(d, bx, by));
    int n = residual_block(b, nc, ac_only ? 15 : 16, lv);
    if (n < 0)
        return 0;
    int r = by * 4 + bx;
    d->nz[d->mb * 24 + r] = (uint8_t)n;
    if (n) {
        int16_t *c = d->coef[r];
        int s = qp / 6, m = qp % 6;
        for (int i = 0; i < (ac_only ? 15 : 16); i++)
            if (lv[i]) {
                int pos = ZIGZAG[ac_only ? i + 1 : i];
                c[pos] = (int16_t)(lv[i] * g_dq[m][pos] * (1 << s));
            }
    }
    return 1;
}

static void put_pcm(H264Dec *d, Br *b)
{
    b->pos = (b->pos + 7) & ~7u;
    uint8_t *y = d->cur->p[0] + (ptrdiff_t)d->mby * 16 * d->stride[0] + d->mbx * 16;
    for (int j = 0; j < 16; j++)
        for (int i = 0; i < 16; i++)
            y[j * d->stride[0] + i] = (uint8_t)getn(b, 8);
    for (int pl = 1; pl < 3; pl++) {
        uint8_t *c = d->cur->p[pl] + (ptrdiff_t)d->mby * 8 * d->stride[1] + d->mbx * 8;
        for (int j = 0; j < 8; j++)
            for (int i = 0; i < 8; i++)
                c[j * d->stride[1] + i] = (uint8_t)getn(b, 8);
    }
}

/* the macroblock at d->mb, parsed and reconstructed; 0 when the stream is damaged */
static int decode_macroblock(H264Dec *d, Br *b, int is_p)
{
    const H264Pps *pps = d->ap;
    int mb = d->mb, mbx = d->mbx, mby = d->mby;
    int mbtype = (int)get_ue(b);
    int intra_type = mbtype;
    int kind, cbp = 0, i16_mode = 0, chroma_mode = 0;
    int stride = d->stride[0], cstride = d->stride[1];
    uint8_t *ydst = d->cur->p[0] + (ptrdiff_t)mby * 16 * stride + mbx * 16;
    uint8_t *cdst[2] = { d->cur->p[1] + (ptrdiff_t)mby * 8 * cstride + mbx * 8, d->cur->p[2] + (ptrdiff_t)mby * 8 * cstride + mbx * 8 };
    memset(d->nz + mb * 24, 0, 24);
    d->nzmask[mb] = 0;
    d->cur_done = 0;
    d->slice_of[mb] = (int16_t)d->cur_slice;
    d->dbk_idc[mb] = (uint8_t)d->sl.dbk_idc;
    d->dbk_a[mb] = (int8_t)d->sl.dbk_a;
    d->dbk_b[mb] = (int8_t)d->sl.dbk_b;
    if (is_p) {
        if (mbtype < 5) {
            kind = KIND_INTER;
        } else {
            intra_type = mbtype - 5;
            kind = KIND_NONE;
        }
    } else {
        kind = KIND_NONE;
    }
    if (kind == KIND_NONE) {
        if (intra_type > 25)
            return 0;
        kind = intra_type == 0 ? KIND_I4 : intra_type == 25 ? KIND_PCM : KIND_I16;
    }
    d->kind[mb] = (uint8_t)kind;
    d->pdirty[mb] = 1;
    memset(d->ipred + mb * 16, 2, 16);

    if (kind == KIND_PCM) {
        put_pcm(d, b);
        memset(d->nz + mb * 24, 16, 24);
        d->nzmask[mb] = 0xFFFF;
        d->qp[mb] = 0;
        return b->pos <= b->end;
    }

    if (kind == KIND_INTER) {
        static const struct { uint8_t x, y, w, h; } PART[3][2] = { { { 0, 0, 4, 4 }, { 0, 0, 0, 0 } }, { { 0, 0, 4, 2 }, { 0, 2, 4, 2 } }, { { 0, 0, 2, 4 }, { 2, 0, 2, 4 } } };
        int nref = d->sl.nref[0], ref[4] = { 0, 0, 0, 0 };
        if (mbtype < 3) {
            int np = mbtype == 0 ? 1 : 2;
            for (int p = 0; p < np; p++) {
                if (nref > 1)
                    ref[p] = nref == 2 ? 1 - get1(b) : (int)get_ue(b);
                if (ref[p] >= nref)
                    return 0;
            }
            for (int p = 0; p < np; p++) {
                int px, py;
                predict_mv(d, PART[mbtype][p].x, PART[mbtype][p].y, PART[mbtype][p].w, ref[p], mbtype, p, &px, &py);
                int mx = px + get_se(b), my = py + get_se(b);
                set_motion(d, PART[mbtype][p].x, PART[mbtype][p].y, PART[mbtype][p].w, PART[mbtype][p].h, ref[p], mx, my);
                for (int y = PART[mbtype][p].y; y < PART[mbtype][p].y + PART[mbtype][p].h; y += 2)
                    for (int x = PART[mbtype][p].x; x < PART[mbtype][p].x + PART[mbtype][p].w; x += 2)
                        d->refi[mb * 4 + (y >> 1) * 2 + (x >> 1)] = (int8_t)ref[p];
                predict_inter(d, PART[mbtype][p].x, PART[mbtype][p].y, PART[mbtype][p].w, PART[mbtype][p].h, ref[p], mx, my);
            }
        } else {
            int sub[4];
            for (int i = 0; i < 4; i++) {
                sub[i] = (int)get_ue(b);
                if (sub[i] > 3)
                    return 0;
            }
            for (int i = 0; i < 4; i++) {
                if (nref > 1 && mbtype != 4)
                    ref[i] = nref == 2 ? 1 - get1(b) : (int)get_ue(b);
                if (ref[i] >= nref)
                    return 0;
                d->refi[mb * 4 + i] = (int8_t)ref[i];
            }
            for (int i = 0; i < 4; i++) {
                int ox = (i & 1) * 2, oy = (i >> 1) * 2;
                int np = sub[i] == 0 ? 1 : sub[i] == 3 ? 4 : 2;
                int pw = sub[i] == 0 || sub[i] == 1 ? 2 : 1, ph = sub[i] == 0 || sub[i] == 2 ? 2 : 1;
                for (int p = 0; p < np; p++) {
                    int px0 = ox + (sub[i] == 2 ? p : sub[i] == 3 ? (p & 1) : 0), py0 = oy + (sub[i] == 1 ? p : sub[i] == 3 ? (p >> 1) : 0);
                    int px, py;
                    predict_mv(d, px0, py0, pw, ref[i], 0, 0, &px, &py);
                    int mx = px + get_se(b), my = py + get_se(b);
                    set_motion(d, px0, py0, pw, ph, ref[i], mx, my);
                    predict_inter(d, px0, py0, pw, ph, ref[i], mx, my);
                }
            }
        }
        for (int i = 0; i < 4; i++)
            d->refpic[mb * 4 + i] = d->rl[d->refi[mb * 4 + i]]->id;
        {
            const int16_t *m = d->mv + mb * 32;
            int uni = 1;
            for (int i = 1; i < 16 && uni; i++)
                uni = m[i * 2] == m[0] && m[i * 2 + 1] == m[1];
            for (int i = 1; i < 4 && uni; i++)
                uni = d->refpic[mb * 4 + i] == d->refpic[mb * 4];
            d->uni[mb] = (uint8_t)uni;
        }
        unsigned code = get_ue(b);
        if (code > 47)
            return 0;
        cbp = CBP_INTER[code];
    } else {
        if (kind == KIND_I4) {
            int8_t *ip = d->ipred + mb * 16;
            int a_ok = mbx > 0 && mb_avail(d, mb - 1), b_ok = mby > 0 && mb_avail(d, mb - d->mbw);
            int a_intra = a_ok && !(pps->constrained && d->kind[mb - 1] == KIND_INTER);
            int b_intra = b_ok && !(pps->constrained && d->kind[mb - d->mbw] == KIND_INTER);
            for (int blk = 0; blk < 16; blk++) {
                int bx = BLK_X[blk], by = BLK_Y[blk];
                int ma, mb2;
                if (bx > 0)
                    ma = ip[by * 4 + bx - 1];
                else
                    ma = a_ok ? (d->kind[mb - 1] == KIND_I4 ? d->ipred[(mb - 1) * 16 + by * 4 + 3] : 2) : -1;
                if (by > 0)
                    mb2 = ip[(by - 1) * 4 + bx];
                else
                    mb2 = b_ok ? (d->kind[mb - d->mbw] == KIND_I4 ? d->ipred[(mb - d->mbw) * 16 + 12 + bx] : 2) : -1;
                int pred;
                if ((bx == 0 && !a_intra) || (by == 0 && !b_intra))
                    pred = 2; /* (a neighbour that is not there, or an inter one with constrained intra prediction) */
                else
                    pred = ma < mb2 ? ma : mb2;
                int mode;
                if (get1(b)) {
                    mode = pred;
                } else {
                    int rem = (int)getn(b, 3);
                    mode = rem < pred ? rem : rem + 1;
                }
                ip[by * 4 + bx] = (int8_t)mode;
            }
        } else {
            i16_mode = (intra_type - 1) & 3;
        }
        chroma_mode = (int)get_ue(b);
        if (chroma_mode > 3)
            return 0;
        if (kind == KIND_I16) {
            cbp = (((intra_type - 1) >> 2) % 3) << 4 | (intra_type > 12 ? 15 : 0);
        } else {
            unsigned code = get_ue(b);
            if (code > 47)
                return 0;
            cbp = CBP_INTRA[code];
        }
    }

    /* the residual */
    int qp = d->qp_cur;
    if (cbp > 0 || kind == KIND_I16) {
        int delta = get_se(b);
        if (delta < -26 || delta > 25)
            return 0;
        qp = (qp + delta + 52) % 52;
        d->qp_cur = qp;
    }
    d->qp[mb] = (uint8_t)qp;
    if (kind == KIND_I16) {
        int lv[16], nc = nc_of(nz_left(d, 0, 0), nz_top(d, 0, 0));
        int n = residual_block(b, nc, 16, lv);
        if (n < 0)
            return 0;
        memset(d->ydc, 0, sizeof(d->ydc));
        for (int i = 0; i < 16; i++)
            d->ydc[ZIGZAG[i]] = (int16_t)lv[i];
        if (n)
            h264_dequant_luma_dc(d->ydc, qp);
    }
    int luma_cbp = cbp & 15, chroma_cbp = cbp >> 4;
    for (int blk = 0; blk < 16; blk++) {
        if (luma_cbp & (1 << (blk >> 2))) {
            if (!read_luma_block(d, b, blk, kind == KIND_I16, qp))
                return 0;
        }
    }
    for (int blk = 0; blk < 16; blk++)
        if (d->nz[mb * 24 + BLK_Y[blk] * 4 + BLK_X[blk]])
            d->nzmask[mb] |= (uint16_t)(1u << (BLK_Y[blk] * 4 + BLK_X[blk]));
    int cqp = h264_chroma_qp[clip_qp(qp + pps->cqp_off)];
    int has_cdc[2] = { 0, 0 };
    if (chroma_cbp) {
        for (int pl = 0; pl < 2; pl++) {
            int lv[4];
            int n = residual_block(b, -1, 4, lv);
            if (n < 0)
                return 0;
            for (int i = 0; i < 4; i++)
                d->cdc[pl][i] = (int16_t)lv[i];
            if (n) {
                h264_dequant_chroma_dc(d->cdc[pl], cqp);
                has_cdc[pl] = 1;
            }
        }
        if (chroma_cbp == 2) {
            for (int pl = 0; pl < 2; pl++)
                for (int blk = 0; blk < 4; blk++) {
                    int bx = blk & 1, by = blk >> 1, lv[16];
                    int nc = nc_of(nz_left_c(d, pl, bx, by), nz_top_c(d, pl, bx, by));
                    int n = residual_block(b, nc, 15, lv);
                    if (n < 0)
                        return 0;
                    d->nz[mb * 24 + 16 + pl * 4 + blk] = (uint8_t)n;
                    if (n) {
                        int s = cqp / 6, m = cqp % 6;
                        for (int i = 0; i < 15; i++)
                            if (lv[i]) {
                                int pos = ZIGZAG[i + 1];
                                d->cac[pl][blk][pos] = (int16_t)(lv[i] * g_dq[m][pos] * (1 << s));
                            }
                    }
                }
        }
    }
    if (b->pos > b->end)
        return 0;

    /* reconstruction */
    if (kind == KIND_I16) {
        int a_ok = mbx > 0 && intra_ok(d, mb - 1), b_ok = mby > 0 && intra_ok(d, mb - d->mbw);
        h264_intra16_pred(ydst, stride, i16_mode, a_ok, b_ok);
        for (int r = 0; r < 16; r++) {
            int16_t *c = d->coef[r];
            c[0] = d->ydc[r];
            if (d->nz[mb * 24 + r])
                h264_idct_add(ydst + (r >> 2) * 4 * stride + (r & 3) * 4, stride, c, 0);
            else if (c[0])
                h264_idct_add(ydst + (r >> 2) * 4 * stride + (r & 3) * 4, stride, c, 1);
        }
    } else if (kind == KIND_I4) {
        int a_ok = mbx > 0 && intra_ok(d, mb - 1), b_ok = mby > 0 && intra_ok(d, mb - d->mbw);
        int c_ok = mby > 0 && mbx + 1 < d->mbw && intra_ok(d, mb - d->mbw + 1), dd_ok = mby > 0 && mbx > 0 && intra_ok(d, mb - d->mbw - 1);
        for (int blk = 0; blk < 16; blk++) {
            int bx = BLK_X[blk], by = BLK_Y[blk], r = by * 4 + bx;
            int hl = bx > 0 || a_ok, ht = by > 0 || b_ok;
            int htl = bx > 0 && by > 0 ? 1 : bx == 0 && by > 0 ? a_ok : by == 0 && bx > 0 ? b_ok : dd_ok;
            int htr;
            if (by == 0)
                htr = bx < 3 ? b_ok : c_ok;
            else if (bx == 3)
                htr = 0;
            else { /* the block up and to the right is decoded before this one when it comes first in the order of the stream */
                int zc = ((by >> 1) << 3) | ((bx >> 1) << 2) | ((by & 1) << 1) | (bx & 1);
                int zt = (((by - 1) >> 1) << 3) | (((bx + 1) >> 1) << 2) | (((by - 1) & 1) << 1) | ((bx + 1) & 1);
                htr = zt < zc;
            }
            uint8_t *dst = ydst + by * 4 * stride + bx * 4;
            h264_intra4_pred(dst, stride, d->ipred[mb * 16 + r], hl, ht, htl, htr);
            if (d->nz[mb * 24 + r]) {
                int16_t *c = d->coef[r];
                h264_idct_add(dst, stride, c, d->nz[mb * 24 + r] == 1 && c[0] != 0); /* (one coefficient, and it is the first: DC only) */
            }
        }
    } else { /* inter: the prediction is in the picture already */
        for (int r = 0; r < 16; r++)
            if (d->nz[mb * 24 + r]) {
                int16_t *c = d->coef[r];
                h264_idct_add(ydst + (r >> 2) * 4 * stride + (r & 3) * 4, stride, c, d->nz[mb * 24 + r] == 1 && c[0] != 0);
            }
    }
    if (kind != KIND_INTER) {
        int a_ok = mbx > 0 && intra_ok(d, mb - 1), b_ok = mby > 0 && intra_ok(d, mb - d->mbw);
        h264_intra_chroma_pred(cdst[0], cstride, chroma_mode, a_ok, b_ok);
        h264_intra_chroma_pred(cdst[1], cstride, chroma_mode, a_ok, b_ok);
    }
    if (chroma_cbp) {
        for (int pl = 0; pl < 2; pl++)
            for (int blk = 0; blk < 4; blk++) {
                int16_t *c = d->cac[pl][blk];
                c[0] = has_cdc[pl] ? d->cdc[pl][blk] : 0;
                uint8_t *dst = cdst[pl] + (blk >> 1) * 4 * cstride + (blk & 1) * 4;
                if (d->nz[mb * 24 + 16 + pl * 4 + blk]) {
                    h264_idct_add(dst, cstride, c, 0);
                } else if (c[0]) {
                    h264_idct_add(dst, cstride, c, 1);
                }
            }
    }
    (void)is_p;
    return 1;
}

/* a skipped macroblock of a P slice */
static int skip_macroblock(H264Dec *d)
{
    int mb = d->mb, mbx = d->mbx, mby = d->mby;
    memset(d->nz + mb * 24, 0, 24);
    d->nzmask[mb] = 0;
    d->cur_done = 0;
    d->slice_of[mb] = (int16_t)d->cur_slice;
    d->kind[mb] = KIND_INTER;
    d->qp[mb] = (uint8_t)d->qp_cur;
    d->dbk_idc[mb] = (uint8_t)d->sl.dbk_idc;
    d->dbk_a[mb] = (int8_t)d->sl.dbk_a;
    d->dbk_b[mb] = (int8_t)d->sl.dbk_b;
    memset(d->ipred + mb * 16, 2, 16);
    int ax, ay, bxv, byv, mx = 0, my = 0;
    int ra = nb_motion(d, -1, 0, &ax, &ay), rb = nb_motion(d, 0, -1, &bxv, &byv);
    if (ra == -2 || rb == -2 || (ra == 0 && !ax && !ay) || (rb == 0 && !bxv && !byv)) {
        mx = my = 0;
    } else {
        predict_mv(d, 0, 0, 4, 0, 0, 0, &mx, &my);
    }
    for (int i = 0; i < 4; i++) {
        d->refi[mb * 4 + i] = 0;
        d->refpic[mb * 4 + i] = d->rl[0]->id;
    }
    set_motion(d, 0, 0, 4, 4, 0, mx, my);
    d->uni[mb] = 1;
    d->pdirty[mb] = !(!mx && !my && d->rl[0]->id == d->prev_id); /* a skipped macroblock that copies the one at the same place of the picture before is not changed */
    predict_inter(d, 0, 0, 4, 4, 0, mx, my);
    (void)mbx; (void)mby;
    return 1;
}

/* ---------------------------------------------------------------------- */
/* slices and pictures */

static int parse_slice_header(H264Dec *d, Br *b, int nal_type, int ref_idc, H264Slice *s)
{
    memset(s, 0, sizeof(*s));
    s->nal_ref_idc = ref_idc;
    s->idr = nal_type == 5;
    s->first_mb = (int)get_ue(b);
    int t = (int)get_ue(b);
    s->type = t % 5;
    s->pps_id = (int)get_ue(b);
    if (s->pps_id > 15 || !d->pps[s->pps_id].valid)
        return 0;
    const H264Pps *pps = &d->pps[s->pps_id];
    if (!d->sps[pps->sps_id].valid)
        return 0;
    const H264Sps *sps = &d->sps[pps->sps_id];
    if (s->type == 1) { d->why = H264_B_SLICES; return 0; }
    if (s->type > 2) { d->why = H264_FORMAT; return 0; } /* SP and SI slices */
    if (pps->cabac) { d->why = H264_CABAC; return 0; }
    if (s->type == 0 && pps->weighted) { d->why = H264_WEIGHTED; return 0; }
    s->frame_num = (int)getn(b, sps->log2_max_fn);
    if (s->idr)
        s->idr_id = (int)get_ue(b);
    if (sps->poc_type == 0) {
        getn(b, sps->log2_max_poc);
        if (pps->bottom_poc)
            get_se(b);
    } else if (sps->poc_type == 1 && !sps->always_zero) {
        get_se(b);
        if (pps->bottom_poc)
            get_se(b);
    }
    if (pps->redundant)
        if (get_ue(b))
            return 0;
    s->nref[0] = pps->nref[0];
    if (s->type == 0) {
        if (get1(b)) {
            s->nref[0] = (int)get_ue(b) + 1;
            if (s->nref[0] > H264_MAXREF)
                return 0;
        }
        if (get1(b)) { /* ref_pic_list_modification_flag_l0 */
            for (;;) {
                int idc = (int)get_ue(b);
                if (idc == 3)
                    break;
                if (idc > 3 || s->reorder_n >= H264_MAXREF)
                    return 0;
                s->reorder[s->reorder_n].idc = idc;
                s->reorder[s->reorder_n].val = (int)get_ue(b);
                s->reorder_n++;
                if (b->pos > b->end)
                    return 0;
            }
        }
    }
    if (ref_idc) {
        if (s->idr) {
            get1(b); /* no_output_of_prior_pics_flag */
            s->long_term_ref = get1(b);
        } else if (get1(b)) {
            s->adaptive = 1;
            for (;;) {
                int op = (int)get_ue(b);
                if (!op)
                    break;
                if (op > 6 || s->mmco_n >= 16)
                    return 0;
                s->mmco[s->mmco_n].op = op;
                s->mmco[s->mmco_n].a = s->mmco[s->mmco_n].b = 0;
                if (op == 1 || op == 3 || op == 2 || op == 4 || op == 6)
                    s->mmco[s->mmco_n].a = (int)get_ue(b);
                if (op == 3)
                    s->mmco[s->mmco_n].b = (int)get_ue(b);
                s->mmco_n++;
                if (b->pos > b->end)
                    return 0;
            }
        }
    }
    s->qp = pps->qp + get_se(b);
    s->dbk_idc = 0;
    if (pps->dbk_ctrl) {
        s->dbk_idc = (int)get_ue(b);
        if (s->dbk_idc > 2)
            return 0;
        if (s->dbk_idc != 1) {
            s->dbk_a = get_se(b);
            s->dbk_b = get_se(b);
        }
    }
    return s->qp >= 0 && s->qp <= 51 && b->pos <= b->end;
}

static H264Pic *free_picture(H264Dec *d)
{
    for (int i = 0; i < d->ndpb; i++)
        if (!d->dpb[i].ref && &d->dpb[i] != d->cur)
            return &d->dpb[i];
    return NULL;
}

static void finish_picture(H264Dec *d)
{
    if (!d->pic_started)
        return;
    d->pic_started = 0;
    if (!d->fast)
        h264_deblock_picture(d);
    h264_extend_borders(d, d->cur);
    d->out = d->cur;
    /* what this picture changed (and what the loop filter may have touched next to it) is what has to be converted again */
    for (int y = 0; y < d->mbh; y++)
        for (int x = 0; x < d->mbw; x++) {
            int mb = y * d->mbw + x;
            if (d->slice_of[mb] < 0) { /* never decoded: everything is made again */
                d->dirty_all[0] = d->dirty_all[1] = d->dirty_all[2] = 1;
            } else if (d->pdirty[mb]) {
                for (int j = y > 0 ? y - 1 : 0; j <= y + 1 && j < d->mbh; j++)
                    for (int i = x > 0 ? x - 1 : 0; i <= x + 1 && i < d->mbw; i++)
                        for (int k = 0; k < 3; k++)
                            d->acc[k][j * d->mbw + i] = 1;
            }
            d->pdirty[mb] = 0;
        }
    d->prev_id = d->cur->id;
}

static int decode_slice(H264Dec *d, const uint8_t *nal, int len)
{
    int type = nal[0] & 31, ref_idc = (nal[0] >> 5) & 3;
    int n = unescape(d, nal, len);
    if (n < 0)
        return -1;
    Br b = { d->rbsp, 0, (uint32_t)n * 8 };
    H264Slice sl;
    if (!parse_slice_header(d, &b, type, ref_idc, &sl))
        return -1;
    d->sl = sl;
    memset(d->coef, 0, sizeof(d->coef)); /* (a damaged macroblock may have left some) */
    memset(d->cac, 0, sizeof(d->cac));
    const H264Pps *pps = &d->pps[sl.pps_id];
    const H264Sps *sps = &d->sps[pps->sps_id];
    if (!d->pic_started && !activate(d, sps))
        return -1;
    d->ap = pps;
    d->max_fn = 1 << sps->log2_max_fn;
    if (!d->pic_started) { /* the first slice of a picture */
        if (sl.idr) {
            mark_all_unused(d);
            d->have_idr = 1;
        } else if (!d->have_idr) {
            return -1; /* nothing to predict from */
        }
        d->cur = NULL;
        d->cur = free_picture(d);
        if (!d->cur)
            return -1;
        d->cur->id = ++d->next_id;
        d->cur->frame_num = sl.frame_num;
        d->cur->ref = 0;
        for (int i = 0; i < d->nmb; i++)
            d->slice_of[i] = -1;
        d->slices = 0;
        d->pic_started = 1;
    }
    if (sl.type == 0 && !build_ref_list(d))
        return -1;
    d->sl.nref[0] = sl.nref[0];
    d->cur_slice = d->slices++;
    d->qp_cur = sl.qp;
    d->stop_bit = find_stop_bit(d->rbsp, n);
    int mb = sl.first_mb;
    if (mb >= d->nmb)
        return -1;
    int is_p = sl.type == 0;
    for (;;) {
        if (is_p) {
            unsigned run = get_ue(&b);
            if (run > (unsigned)(d->nmb - mb))
                return -1;
            while (run--) {
                d->mb = mb;
                d->mbx = mb % d->mbw;
                d->mby = mb / d->mbw;
                skip_macroblock(d);
                mb++;
            }
            if (!more_data(&b, d->stop_bit))
                break;
        }
        if (mb >= d->nmb)
            break;
        d->mb = mb;
        d->mbx = mb % d->mbw;
        d->mby = mb / d->mbw;
        if (!decode_macroblock(d, &b, is_p))
            return -1;
        mb++;
        if (!more_data(&b, d->stop_bit))
            break;
    }
    return 0;
}

/* ---------------------------------------------------------------------- */
/* the API */

static void read_param_nal(H264Dec *d, const uint8_t *nal, int len)
{
    int type = nal[0] & 31;
    if (type != 7 && type != 8)
        return;
    int n = unescape(d, nal, len);
    if (n < 0)
        return;
    Br b = { d->rbsp, 0, (uint32_t)n * 8 };
    if (type == 7)
        parse_sps(d, &b);
    else
        parse_pps(d, &b, find_stop_bit(d->rbsp, n));
}

H264Dec *h264d_new(const uint8_t *params, int params_len, int nal_size, int *why)
{
    vlc_init();
    dq_init();
    h264_rec_init();
    if (why)
        *why = H264_NO_PARAMS;
    if (nal_size < 1 || nal_size > 4)
        return NULL;
    H264Dec *d = calloc(1, sizeof(*d));
    if (!d)
        return NULL;
    d->nal_size = nal_size;
    int at = 0;
    for (;;) { /* Annex B: a start code (00 00 01, maybe with a zero in front) before each NAL unit */
        int sc = at;
        while (sc + 3 <= params_len && !(params[sc] == 0 && params[sc + 1] == 0 && params[sc + 2] == 1))
            sc++;
        if (sc + 3 > params_len)
            break;
        int start = sc + 3, next = start;
        while (next + 3 <= params_len && !(params[next] == 0 && params[next + 1] == 0 && params[next + 2] == 1))
            next++;
        int end = next + 3 <= params_len ? next : params_len;
        while (end > start && params[end - 1] == 0)
            end--;
        if (end > start)
            read_param_nal(d, params + start, end - start);
        at = next;
        if (next + 3 > params_len)
            break;
    }
    int ok = 0;
    for (int k = 0; k < 8; k++)
        for (int j = 0; j < 16; j++)
            if (d->sps[k].valid && d->pps[j].valid && d->pps[j].sps_id == k)
                ok = 1;
    if (ok) {
        /* the first parameter sets must be ones this decoder can decode */
        for (int j = 0; j < 16; j++) {
            if (!d->pps[j].valid)
                continue;
            if (d->pps[j].cabac) { d->why = H264_CABAC; ok = 0; }
        }
    }
    if (!ok) {
        if (why)
            *why = d->why ? d->why : H264_NO_PARAMS;
        h264d_free(d);
        return NULL;
    }
    for (int j = 0; j < 16; j++) /* the first set that goes with a PPS is the one the sizes are taken from */
        if (d->pps[j].valid && d->sps[d->pps[j].sps_id].valid) {
            if (!activate(d, &d->sps[d->pps[j].sps_id])) {
                if (why)
                    *why = d->why ? d->why : H264_MEMORY;
                h264d_free(d);
                return NULL;
            }
            d->ap = &d->pps[j];
            break;
        }
    if (why)
        *why = H264_OK;
    return d;
}

void h264d_free(H264Dec *d)
{
    if (!d)
        return;
    free_pictures(d);
    free(d->rbsp);
    free(d);
}

void h264d_reset(H264Dec *d)
{
    if (!d->dpb)
        return;
    mark_all_unused(d);
    d->have_idr = 0;
    d->pic_started = 0;
    d->out = NULL;
    d->dirty_all[0] = d->dirty_all[1] = d->dirty_all[2] = 1;
    d->prev_id = 0;
}

int h264d_decode(H264Dec *d, const uint8_t *sample, int len)
{
    int pos = 0, bad = 0;
    d->why = H264_OK;
    d->out = NULL;
    d->pic_started = 0;
    while (pos + d->nal_size <= len) {
        unsigned sz = 0;
        for (int i = 0; i < d->nal_size; i++)
            sz = (sz << 8) | sample[pos + i];
        pos += d->nal_size;
        if (sz == 0 || sz > (unsigned)(len - pos))
            break;
        const uint8_t *nal = sample + pos;
        int type = nal[0] & 31;
        pos += (int)sz;
        if (type == 7 || type == 8) {
            read_param_nal(d, nal, (int)sz);
        } else if (type == 1 || type == 5) {
            if (decode_slice(d, nal, (int)sz) < 0)
                bad = 1;
        } else if (type == 2 || type == 3 || type == 4) {
            d->why = H264_FORMAT; /* data partitioning */
            bad = 1;
        }
    }
    if (d->pic_started) {
        mark_references(d, &d->sl);
        finish_picture(d);
    }
    if (!d->out)
        return bad ? -1 : 0;
    return bad ? -1 : 1;
}

int h264d_why(const H264Dec *d) { return d->why; }
void h264d_set_fast(H264Dec *d, int fast) { d->fast = fast; }
int h264d_width(const H264Dec *d) { return d->width; }
int h264d_height(const H264Dec *d) { return d->height; }
int h264d_nref(const H264Dec *d) { return d->as ? d->as->nref : 0; }

void h264d_copy_planes(const H264Dec *d, unsigned char *y, int ys, unsigned char *u, unsigned char *v, int cs)
{
    const H264Pic *p = d->out;
    if (!p)
        return;
    const H264Sps *s = d->as;
    int x0 = 2 * s->crop_l, y0 = 2 * s->crop_t, w = d->width, h = d->height;
    for (int j = 0; j < h; j++)
        memcpy(y + (ptrdiff_t)j * ys, p->p[0] + (ptrdiff_t)(y0 + j) * d->stride[0] + x0, (size_t)w);
    for (int j = 0; j < (h + 1) / 2; j++) {
        memcpy(u + (ptrdiff_t)j * cs, p->p[1] + (ptrdiff_t)(y0 / 2 + j) * d->stride[1] + x0 / 2, (size_t)(w + 1) / 2);
        memcpy(v + (ptrdiff_t)j * cs, p->p[2] + (ptrdiff_t)(y0 / 2 + j) * d->stride[2] + x0 / 2, (size_t)(w + 1) / 2);
    }
}

/* the picture as 5650 (red in the low bits, the PSP's order); BT.601 with the video range. One table look-up per colour of a pixel: the tables take the sum (the luma plus what the chroma adds) and give the
 * colour clipped and in its place in the 16 bits. */
void h264d_rgb565(const H264Dec *d_, uint16_t *dst, int stride, int slot)
{
    H264Dec *d = (H264Dec *)d_; /* (what has been converted is noted in it) */
    static int16_t ytab[256], rv[256], gu[256], gv[256], bu[256];
    static uint16_t tr[1024], tg[1024], tb[1024]; /* index: sum + 384 */
    static int ready;
    if (!ready) {
        for (int i = 0; i < 256; i++) {
            int y = i - 16;
            ytab[i] = (int16_t)(y < 0 ? 0 : (298 * y + 128) >> 8);
            rv[i] = (int16_t)((409 * (i - 128) + 128) >> 8);
            gu[i] = (int16_t)(100 * (i - 128));
            gv[i] = (int16_t)(208 * (i - 128));
            bu[i] = (int16_t)((516 * (i - 128) + 128) >> 8);
        }
        for (int i = 0; i < 1024; i++) {
            int c = i - 384;
            c = c < 0 ? 0 : c > 255 ? 255 : c;
            tr[i] = (uint16_t)(c >> 3);
            tg[i] = (uint16_t)((c >> 2) << 5);
            tb[i] = (uint16_t)((c >> 3) << 11);
        }
        ready = 1;
    }
    const H264Pic *p = d->out;
    if (!p)
        return;
    const H264Sps *s = d->as;
    int x0 = 2 * s->crop_l, y0 = 2 * s->crop_t;
    int w = d->width, h = d->height;
    int cropped = x0 || y0 || w != d->mbw * 16 || h != d->mbh * 16;
    uint8_t *acc = d->acc[slot];
    int all = d->dirty_all[slot] || cropped;
    for (int my = 0; my < d->mbh; my++)
        for (int mx = 0; mx < d->mbw; mx++) {
            if (!all && !acc[my * d->mbw + mx])
                continue;
            int ya = my * 16, yb = ya + 16, xa = mx * 16, xb = xa + 16;
            if (cropped) { /* the whole picture at once, in the first block */
                if (mx || my)
                    continue;
                ya = 0; yb = h; xa = 0; xb = w;
            }
            for (int y = ya; y < yb; y++) {
                const uint8_t *yp = p->p[0] + (ptrdiff_t)(y0 + y) * d->stride[0] + x0;
                const uint8_t *up = p->p[1] + (ptrdiff_t)((y0 + y) >> 1) * d->stride[1] + (x0 >> 1);
                const uint8_t *vp = p->p[2] + (ptrdiff_t)((y0 + y) >> 1) * d->stride[2] + (x0 >> 1);
                uint16_t *o = dst + (ptrdiff_t)y * stride;
                for (int x = xa; x < xb; x += 2) {
                    int u = up[x >> 1], v = vp[x >> 1];
                    int r = rv[v] + 384, g = 384 - ((gu[u] + gv[v] + 128) >> 8), bl = bu[u] + 384;
                    int y1 = ytab[yp[x]], y2 = ytab[yp[x + 1]];
                    o[x] = (uint16_t)(tb[y1 + bl] | tg[y1 + g] | tr[y1 + r]);
                    o[x + 1] = (uint16_t)(tb[y2 + bl] | tg[y2 + g] | tr[y2 + r]);
                }
            }
        }
    memset(acc, 0, (size_t)d->nmb);
    d->dirty_all[slot] = 0;
}
