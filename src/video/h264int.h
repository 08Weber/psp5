/* psp5 authentic – the H.264 decoder in software: what its parts share (h264dec.c reads the stream, h264rec.c makes the pictures, h264lf.c filters them). Not for use outside of those. */
#ifndef H264INT_H
#define H264INT_H

#include <stdint.h>
#include <stddef.h>

#include "h264dec.h"

#define H264_MAXREF 16
#define PAD_Y 32 /* the border around a reference picture (luma; chroma has half) */
#define PAD_C 16

enum { KIND_NONE = 0, KIND_I4, KIND_I16, KIND_PCM, KIND_INTER };

typedef struct {
    uint8_t *buf;
    uint8_t *p[3]; /* the planes, at their top left corner */
    int frame_num, wrap;
    int ref; /* 0 = not used for reference, 1 = short term, 2 = long term */
    int long_idx;
    int id; /* tells pictures apart for the loop filter */
} H264Pic;

typedef struct {
    int profile, level, id;
    int log2_max_fn, poc_type, log2_max_poc, always_zero, nref, mbw, mbh;
    int frame_mbs_only, direct8x8, crop_l, crop_r, crop_t, crop_b, valid;
} H264Sps;

typedef struct {
    int id, sps_id, cabac, bottom_poc, nslice_groups, nref[2], weighted, weighted_bi, qp, qs, cqp_off, dbk_ctrl, constrained, redundant, valid;
} H264Pps;

typedef struct {
    int first_mb, type, pps_id, frame_num, idr, idr_id, nal_ref_idc;
    int nref[2], qp, dbk_idc, dbk_a, dbk_b;
    int reorder_n;
    struct { int idc, val; } reorder[H264_MAXREF + 1];
    int adaptive, mmco_n, long_term_ref;
    struct { int op, a, b; } mmco[16];
} H264Slice;

struct H264Dec {
    H264Sps sps[8];
    H264Pps pps[16];
    const H264Sps *as; /* the active ones */
    const H264Pps *ap;
    int mbw, mbh, nmb, stride[3], plane_size[3], pic_bytes;
    int width, height; /* what is shown (after cropping) */
    int nal_size;
    /* pictures */
    H264Pic *dpb;
    int ndpb;
    H264Pic *cur;
    H264Pic *out; /* the picture the last sample made */
    int next_id, prev_frame_num, have_idr, max_fn;
    H264Pic *rl[H264_MAXREF + 1];
    int nrl;
    /* the picture being made (per macroblock) */
    int16_t *slice_of; /* the number of the slice a macroblock is in, -1 when it has not been decoded in this picture */
    uint8_t *kind, *qp, *dbk_idc, *uni, *pdirty, *acc[3]; /* pdirty: the macroblocks this picture changed against the one before; acc: those changed since the picture was last converted to RGB, for each of three RGB buffers */
    int8_t *dbk_a, *dbk_b;
    int8_t *ipred;     /* 16 per macroblock: intra 4x4 prediction modes */
    uint8_t *nz;       /* 24 per macroblock: the number of coefficients of the blocks (16 luma in raster order, 4 + 4 chroma) */
    uint16_t *nzmask;  /* the luma blocks with coefficients */
    int16_t *mv;       /* 32 per macroblock (x, y of 16 blocks in raster order) */
    int8_t *refi;      /* 4 per macroblock */
    int *refpic;       /* 4 per macroblock: the id of the picture */
    int mbs_done, cur_slice, slices;
    int pic_started;
    /* the slice */
    H264Slice sl;
    int qp_cur;
    /* the macroblock */
    int mbx, mby, mb;
    uint32_t cur_done; /* the 4x4 blocks of the macroblock whose motion is known */
    int16_t ydc[16], coef[16][16], cdc[2][4], cac[2][4][16];
    int tmp_edge[32 * 32];
    uint8_t scratch[48 * 48 * 2 + 64];
    int err;
    int why;
    int fast;
    int dirty_all[3], prev_id;
    uint8_t *rbsp;
    int rbsp_cap;
    uint32_t stop_bit;
};

/* h264rec.c */
void h264_rec_init(void);
void h264_idct_add(uint8_t *dst, int stride, int16_t *c, int dc_only);
void h264_intra4_pred(uint8_t *dst, int stride, int mode, int has_left, int has_top, int has_tl, int has_tr);
void h264_intra16_pred(uint8_t *dst, int stride, int mode, int has_left, int has_top);
void h264_intra_chroma_pred(uint8_t *dst, int stride, int mode, int has_left, int has_top);
void h264_mc_luma(H264Dec *d, const H264Pic *ref, uint8_t *dst, int stride, int x, int y, int w, int h, int mvx, int mvy);
void h264_mc_chroma(H264Dec *d, const H264Pic *ref, int plane, uint8_t *dst, int stride, int x, int y, int w, int h, int mvx, int mvy);
void h264_dequant_luma_dc(int16_t *dc, int qp);
void h264_dequant_chroma_dc(int16_t *dc, int qp);
extern const uint8_t h264_chroma_qp[52];

/* h264lf.c */
void h264_deblock_picture(H264Dec *d);
void h264_extend_borders(H264Dec *d, H264Pic *p);

#endif
