/* psp5 authentic – the timing the PSP's own H.264 streams have (see h264sei.h) */
#include <string.h>
#include "h264sei.h"

/* ---------------------------------------------------------------------- */
/* bits */

typedef struct {
    const uint8_t *b;
    int bits, pos, err;
} BR;

static unsigned rd(BR *r, int n)
{
    unsigned v = 0;
    for (int i = 0; i < n; i++) {
        int bit = 0;
        if (r->pos >= r->bits)
            r->err = 1;
        else
            bit = (r->b[r->pos >> 3] >> (7 - (r->pos & 7))) & 1;
        r->pos++;
        v = (v << 1) | (unsigned)bit;
    }
    return v;
}

static unsigned rd_ue(BR *r)
{
    int zeros = 0;
    while (!r->err && rd(r, 1) == 0)
        if (++zeros > 31) {
            r->err = 1;
            return 0;
        }
    return zeros ? ((1u << zeros) - 1) + rd(r, zeros) : 0;
}

static int rd_se(BR *r)
{
    unsigned k = rd_ue(r);
    return k & 1 ? (int)((k + 1) / 2) : -(int)(k / 2);
}

typedef struct {
    uint8_t *b;
    int cap, pos, err; /* cap in bytes, pos in bits */
} BW;

static void wr(BW *w, unsigned v, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        if ((w->pos >> 3) >= w->cap) {
            w->err = 1;
            return;
        }
        if (w->pos % 8 == 0)
            w->b[w->pos >> 3] = 0;
        if ((v >> i) & 1)
            w->b[w->pos >> 3] |= (uint8_t)(0x80 >> (w->pos & 7));
        w->pos++;
    }
}

static void wr_ue(BW *w, unsigned v)
{
    unsigned x = v + 1;
    int len = 0;
    while ((x >> len) > 1)
        len++;
    wr(w, 0, len);
    wr(w, x, len + 1);
}

/* the byte after "00 00" that would look like a start code gets 03 in front of it */
static int escape(const uint8_t *in, int n, uint8_t *out, int cap)
{
    int o = 0, zeros = 0;
    for (int i = 0; i < n; i++) {
        if (zeros >= 2 && in[i] <= 3) {
            if (o >= cap)
                return -1;
            out[o++] = 3;
            zeros = 0;
        }
        if (o >= cap)
            return -1;
        out[o++] = in[i];
        zeros = in[i] == 0 ? zeros + 1 : 0;
    }
    return o;
}

static int unescape(const uint8_t *in, int n, uint8_t *out)
{
    int o = 0, zeros = 0;
    for (int i = 0; i < n; i++) {
        if (zeros >= 2 && in[i] == 3) {
            zeros = 0;
            continue;
        }
        out[o++] = in[i];
        zeros = in[i] == 0 ? zeros + 1 : 0;
    }
    return o;
}

/* ---------------------------------------------------------------------- */
/* the SPS */

#define SPS_MAX 256

static void skip_scaling_list(BR *r, int size)
{
    int last = 8, next = 8;
    for (int j = 0; j < size && !r->err; j++) {
        if (next != 0)
            next = (last + rd_se(r) + 256) % 256;
        last = next == 0 ? last : next;
    }
}

/* the position of vui_parameters_present_flag in the RBSP of an SPS, -1 when it cannot be read */
static int sps_vui_pos(BR *r)
{
    int profile = (int)rd(r, 8);
    rd(r, 16); /* the constraint flags and the level */
    rd_ue(r);
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 || profile == 83 || profile == 86 || profile == 118 || profile == 128 || profile == 138 ||
        profile == 139 || profile == 134 || profile == 135) {
        unsigned chroma = rd_ue(r);
        if (chroma == 3)
            rd(r, 1);
        rd_ue(r);
        rd_ue(r);
        rd(r, 1);
        if (rd(r, 1)) {
            int n = chroma != 3 ? 8 : 12;
            for (int i = 0; i < n && !r->err; i++)
                if (rd(r, 1))
                    skip_scaling_list(r, i < 6 ? 16 : 64);
        }
    }
    rd_ue(r);
    unsigned poc = rd_ue(r);
    if (poc == 0) {
        rd_ue(r);
    } else if (poc == 1) {
        rd(r, 1);
        rd_se(r);
        rd_se(r);
        unsigned n = rd_ue(r);
        for (unsigned i = 0; i < n && !r->err; i++)
            rd_se(r);
    }
    rd_ue(r);
    rd(r, 1);
    rd_ue(r);
    rd_ue(r);
    if (!rd(r, 1))
        rd(r, 1);
    rd(r, 1);
    if (rd(r, 1))
        for (int i = 0; i < 4; i++)
            rd_ue(r);
    return r->err ? -1 : r->pos;
}

static void skip_hrd(BR *r)
{
    unsigned cnt = rd_ue(r) + 1;
    rd(r, 8);
    for (unsigned i = 0; i < cnt && i < 32 && !r->err; i++) {
        rd_ue(r);
        rd_ue(r);
        rd(r, 1);
    }
    rd(r, 20);
}

int h264_sps_has_hrd(const uint8_t *nal, int len)
{
    uint8_t rbsp[SPS_MAX];
    if (len < 5 || len > SPS_MAX || (nal[0] & 0x1F) != 7)
        return 0;
    int n = unescape(nal + 1, len - 1, rbsp);
    BR r = { rbsp, n * 8, 0, 0 };
    if (sps_vui_pos(&r) < 0 || !rd(&r, 1))
        return 0; /* no VUI at all */
    if (rd(&r, 1)) { /* the aspect ratio */
        if (rd(&r, 8) == 255)
            rd(&r, 32);
    }
    if (rd(&r, 1))
        rd(&r, 1);
    if (rd(&r, 1)) {
        rd(&r, 4); /* the format and the full range flag */
        if (rd(&r, 1))
            rd(&r, 24);
    }
    if (rd(&r, 1)) {
        rd_ue(&r);
        rd_ue(&r);
    }
    if (rd(&r, 1))
        rd(&r, 65);
    int nal_hrd = (int)rd(&r, 1);
    if (nal_hrd)
        skip_hrd(&r);
    int vcl_hrd = (int)rd(&r, 1);
    return r.err ? 0 : (nal_hrd || vcl_hrd);
}

/* the HRD of the PSP's own streams: 640 kbit/s and a buffer of 640 kbit, delays of 18, 7 and 7 bits, a time offset of 24 */
static void put_hrd(BW *w)
{
    wr_ue(w, 0); /* one buffer */
    wr(w, 4, 4); /* bit_rate_scale */
    wr(w, 6, 4); /* cpb_size_scale */
    wr_ue(w, 624);
    wr_ue(w, 624);
    wr(w, 0, 1); /* not constant bit rate */
    wr(w, 17, 5); /* initial_cpb_removal_delay_length - 1 */
    wr(w, 6, 5);  /* cpb_removal_delay_length - 1 */
    wr(w, 6, 5);  /* dpb_output_delay_length - 1 */
    wr(w, 24, 5); /* time_offset_length */
}

/* what the VUI of an SPS says that the new one keeps: the aspect ratio, and the video format, range and colours */
typedef struct {
    int aspect_present, aspect_idc;
    unsigned sar_w, sar_h;
    int signal_present, video_format, full_range, colour_present;
    unsigned cp, tc, mc;
} OldVui;

static void read_old_vui(BR *r, OldVui *v)
{
    memset(v, 0, sizeof(*v));
    v->aspect_present = 1; /* (without a VUI: square pixels, limited range, the PSP's format number) */
    v->aspect_idc = 1;
    v->signal_present = 1;
    v->video_format = 5;
    if (!rd(r, 1) || r->err)
        return; /* no VUI */
    v->aspect_present = (int)rd(r, 1);
    if (v->aspect_present) {
        v->aspect_idc = (int)rd(r, 8);
        if (v->aspect_idc == 255) {
            v->sar_w = rd(r, 16);
            v->sar_h = rd(r, 16);
        }
    }
    if (rd(r, 1))
        rd(r, 1); /* overscan */
    v->signal_present = (int)rd(r, 1);
    v->video_format = 5;
    if (v->signal_present) {
        v->video_format = (int)rd(r, 3);
        v->full_range = (int)rd(r, 1);
        v->colour_present = (int)rd(r, 1);
        if (v->colour_present) {
            v->cp = rd(r, 8);
            v->tc = rd(r, 8);
            v->mc = rd(r, 8);
        }
    }
    if (r->err) { /* (cut short: the format and range are not to be trusted) */
        v->signal_present = 1;
        v->video_format = 5;
        v->full_range = 0;
        v->colour_present = 0;
    }
}

int h264_sps_relabel(const uint8_t *nal, int len, uint8_t *out, int cap)
{
    uint8_t rbsp[SPS_MAX];
    if (len < 5 || len > SPS_MAX || (nal[0] & 0x1F) != 7 || cap < 1)
        return -1;
    int n = unescape(nal + 1, len - 1, rbsp);
    if (rbsp[0] == 66) {
        rbsp[0] = 77;
        rbsp[1] = 0x40;
        if (rbsp[2] < 21)
            rbsp[2] = 21;
    }
    out[0] = nal[0];
    int e = escape(rbsp, n, out + 1, cap - 1);
    return e < 0 ? -1 : e + 1;
}

/* Main (77) or Baseline (66) -> Baseline: the SPS of both has the same fields */
static void to_baseline(uint8_t *rbsp, int n)
{
    if (n >= 3 && (rbsp[0] == 66 || rbsp[0] == 77)) {
        rbsp[0] = 66;
        rbsp[1] = 0xC0; /* constraint_set0_flag and constraint_set1_flag, as x264 writes them for Baseline */
    }
}

int h264_sps_to_base(const uint8_t *nal, int len, uint8_t *out, int cap)
{
    uint8_t rbsp[SPS_MAX];
    if (len < 5 || len > SPS_MAX || (nal[0] & 0x1F) != 7 || cap < 1)
        return -1;
    int n = unescape(nal + 1, len - 1, rbsp);
    to_baseline(rbsp, n);
    out[0] = nal[0];
    int e = escape(rbsp, n, out + 1, cap - 1);
    return e < 0 ? -1 : e + 1;
}

int h264_pps_info(const uint8_t *nal, int len, int *cabac, int *nref0, int *weighted)
{
    uint8_t rbsp[64];
    *cabac = *nref0 = *weighted = 0;
    if (len < 4 || len > (int)sizeof(rbsp) + 1 || (nal[0] & 0x1F) != 8)
        return 1;
    int n = unescape(nal + 1, len - 1, rbsp);
    BR r = { rbsp, n * 8, 0, 0 };
    rd_ue(&r);
    rd_ue(&r);
    *cabac = (int)rd(&r, 1);
    rd(&r, 1);
    unsigned groups = rd_ue(&r);
    if (groups == 0) {
        *nref0 = (int)rd_ue(&r) + 1;
        rd_ue(&r);
        *weighted = (int)rd(&r, 1);
    }
    return r.err || groups != 0;
}

int h264_sps_info(const uint8_t *nal, int len, int *profile, int *nref, int *wmb, int *hmb, int *poc_type)
{
    uint8_t rbsp[SPS_MAX];
    if (len < 5 || len > SPS_MAX || (nal[0] & 0x1F) != 7)
        return 0;
    int n = unescape(nal + 1, len - 1, rbsp);
    *profile = rbsp[0];
    BR r = { rbsp, n * 8, 0, 0 };
    /* the same walk as sps_vui_pos(), keeping what is wanted */
    rd(&r, 24);
    rd_ue(&r);
    if (*profile >= 100) { /* (the High profiles: their extra fields are not looked at: only what comes first) */
        *nref = *wmb = *hmb = *poc_type = -1;
        return 1;
    }
    rd_ue(&r);
    *poc_type = (int)rd_ue(&r);
    if (*poc_type == 0) {
        rd_ue(&r);
    } else if (*poc_type == 1) {
        rd(&r, 1);
        rd_se(&r);
        rd_se(&r);
        unsigned k = rd_ue(&r);
        for (unsigned i = 0; i < k && !r.err; i++)
            rd_se(&r);
    }
    *nref = (int)rd_ue(&r);
    rd(&r, 1);
    *wmb = (int)rd_ue(&r) + 1;
    *hmb = (int)rd_ue(&r) + 1;
    return !r.err;
}

int h264_sps_rewrite(const uint8_t *nal, int len, H264Tick tick, uint8_t *out, int cap)
{
    uint8_t rbsp[SPS_MAX], tmp[SPS_MAX + 64];
    if (len < 5 || len > SPS_MAX || (nal[0] & 0x1F) != 7 || tick.num_units == 0 || tick.time_scale == 0)
        return -1;
    int n = unescape(nal + 1, len - 1, rbsp);
    if (tick.to_main && n >= 3 && rbsp[0] == 66) { /* Baseline -> Main */
        rbsp[0] = 77;
        rbsp[1] = 0x40; /* constraint_set1_flag, as the PSP's own streams */
        if (rbsp[2] < 21)
            rbsp[2] = 21;
    }
    BR r = { rbsp, n * 8, 0, 0 };
    int vui = sps_vui_pos(&r);
    if (vui < 0)
        return -1;
    OldVui old;
    read_old_vui(&r, &old); /* r is at the flag now */
    BW w = { tmp, (int)sizeof(tmp), 0, 0 };
    for (int i = 0; i < vui; i++) /* everything before the VUI as it was */
        wr(&w, (rbsp[i >> 3] >> (7 - (i & 7))) & 1, 1);
    wr(&w, 1, 1);          /* vui_parameters_present_flag */
    wr(&w, (unsigned)old.aspect_present, 1); /* aspect_ratio_info_present_flag */
    if (old.aspect_present) {
        wr(&w, (unsigned)old.aspect_idc, 8);
        if (old.aspect_idc == 255) {
            wr(&w, old.sar_w, 16);
            wr(&w, old.sar_h, 16);
        }
    }
    wr(&w, 1, 1);          /* overscan_info_present_flag */
    wr(&w, 0, 1);          /* overscan_appropriate_flag */
    wr(&w, (unsigned)old.signal_present, 1); /* video_signal_type_present_flag */
    if (old.signal_present) {
        wr(&w, (unsigned)old.video_format, 3);
        wr(&w, (unsigned)old.full_range, 1);
        wr(&w, (unsigned)old.colour_present, 1);
        if (old.colour_present) {
            wr(&w, old.cp, 8);
            wr(&w, old.tc, 8);
            wr(&w, old.mc, 8);
        }
    }
    wr(&w, 0, 1);          /* chroma_loc_info_present_flag */
    wr(&w, 1, 1);          /* timing_info_present_flag */
    wr(&w, tick.num_units, 32);
    wr(&w, tick.time_scale, 32);
    wr(&w, 1, 1);          /* fixed_frame_rate_flag */
    wr(&w, 1, 1);          /* nal_hrd_parameters_present_flag */
    put_hrd(&w);
    wr(&w, 1, 1);          /* vcl_hrd_parameters_present_flag */
    put_hrd(&w);
    wr(&w, 0, 1);          /* low_delay_hrd_flag */
    wr(&w, 1, 1);          /* pic_struct_present_flag */
    wr(&w, 0, 1);          /* bitstream_restriction_flag */
    wr(&w, 1, 1);          /* rbsp_stop_one_bit */
    while (w.pos & 7)
        wr(&w, 0, 1);
    if (w.err)
        return -1;
    if (cap < 1)
        return -1;
    out[0] = nal[0];
    int e = escape(tmp, w.pos >> 3, out + 1, cap - 1);
    return e < 0 ? -1 : e + 1;
}

/* ---------------------------------------------------------------------- */
/* the SEI messages */

static int sei_nal(int type, const uint8_t *payload, int size, uint8_t *out, int cap)
{
    uint8_t rbsp[40];
    if (size + 3 > (int)sizeof(rbsp))
        return -1;
    rbsp[0] = (uint8_t)type;
    rbsp[1] = (uint8_t)size;
    memcpy(rbsp + 2, payload, (size_t)size);
    rbsp[2 + size] = 0x80;
    if (cap < 1)
        return -1;
    out[0] = 0x06;
    int e = escape(rbsp, size + 3, out + 1, cap - 1);
    return e < 0 ? -1 : e + 1;
}

int h264_sei_buffering(uint8_t *out, int cap)
{
    uint8_t p[16];
    BW w = { p, (int)sizeof(p), 0, 0 };
    wr_ue(&w, 0);       /* seq_parameter_set_id */
    for (int i = 0; i < 2; i++) { /* the NAL HRD and the VCL HRD */
        wr(&w, 90000, 18); /* initial_cpb_removal_delay */
        wr(&w, 0, 18);     /* initial_cpb_removal_delay_offset */
    }
    wr(&w, 1, 1); /* payload_bit_equal_to_one */
    while (w.pos & 7)
        wr(&w, 0, 1);
    return w.err ? -1 : sei_nal(0, p, w.pos >> 3, out, cap);
}

int h264_sei_pic_timing(unsigned cpb_removal_delay, unsigned dpb_output_delay, uint8_t *out, int cap)
{
    uint8_t p[16];
    BW w = { p, (int)sizeof(p), 0, 0 };
    wr(&w, cpb_removal_delay & 127, 7);
    wr(&w, dpb_output_delay > 127 ? 127 : dpb_output_delay, 7);
    wr(&w, 0, 4); /* pic_struct: a frame */
    wr(&w, 1, 1); /* clock_timestamp_flag */
    wr(&w, 0, 2); /* ct_type */
    wr(&w, 1, 1); /* nuit_field_based_flag */
    wr(&w, 3, 5); /* counting_type */
    wr(&w, 0, 1); /* full_timestamp_flag */
    wr(&w, 1, 1); /* discontinuity_flag */
    wr(&w, 0, 1); /* cnt_dropped_flag */
    wr(&w, 0, 8); /* n_frames */
    wr(&w, 0, 1); /* seconds_flag */
    wr(&w, 0, 24); /* time_offset */
    wr(&w, 1, 1); /* payload_bit_equal_to_one */
    while (w.pos & 7)
        wr(&w, 0, 1);
    return w.err ? -1 : sei_nal(1, p, w.pos >> 3, out, cap);
}

int h264_sei_recovery(uint8_t *out, int cap)
{
    static const uint8_t p[1] = { 0xC4 }; /* recovery_frame_cnt 0, exact match, no broken link, one payload bit and two zeros */
    return sei_nal(6, p, 1, out, cap);
}
