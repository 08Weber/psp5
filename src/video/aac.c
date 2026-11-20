/* psp5 authentic – AAC-LC decoder (see aac.h). Written from the standard (ISO/IEC 14496-3, the syntax of the raw data block, the spectral tools and the filter bank); the tables are in aactab.inc and
 * are data of the standard. Floating point throughout (the PSP's CPU has an FPU). */
#include "aac.h"
#include "downmix.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "aactab.inc"

#define MAXW 8   /* windows of a frame (short blocks) */
#define MAXB 64  /* scale factor bands */
static int g_err_line; /* the line of aac.c that found the last frame damaged or not supported (for the log) */
#define BAD() (g_err_line = __LINE__, 0)
#define ZERO_HCB 0
#define ESC_HCB 11
#define NOISE_HCB 13
#define INTENSITY_HCB2 14 /* out of phase */
#define INTENSITY_HCB 15  /* in phase */

enum { ONLY_LONG, LONG_START, EIGHT_SHORT, LONG_STOP };

/* ---------------------------------------------------------------------- */
/* bits */

typedef struct {
    const unsigned char *p; /* the frame, with 16 bytes of zeros after it */
    unsigned pos, end;      /* in bits */
} Bits;

static unsigned peek(const Bits *b, int n) /* n <= 24 */
{
    if (b->pos > b->end + 32) /* far past the end of a damaged frame: zeros (the frame has 16 bytes of them after it) */
        return 0;
    const unsigned char *q = b->p + (b->pos >> 3);
    unsigned v = ((unsigned)q[0] << 24) | ((unsigned)q[1] << 16) | ((unsigned)q[2] << 8) | q[3];
    return (v << (b->pos & 7)) >> (32 - n);
}

static unsigned getb(Bits *b, int n)
{
    if (n == 0)
        return 0;
    unsigned v = peek(b, n);
    b->pos += (unsigned)n;
    return v;
}

static int get1(Bits *b)
{
    if (b->pos > b->end + 32) {
        b->pos++;
        return 0;
    }
    unsigned v = (b->p[b->pos >> 3] >> (7 - (b->pos & 7))) & 1;
    b->pos++;
    return (int)v;
}

/* ---------------------------------------------------------------------- */
/* tables */

typedef struct {
    int maxlen;
    unsigned first[20];
    unsigned short count[20], offset[20];
    unsigned short sym[300];
    unsigned short lut[512]; /* by the next 9 bits: the symbol and, shifted up 9 places, the length of its code, when it is not longer than 9 bits (0: take the slow way) */
} Huff; /* the codes of the standard are canonical: a code of length l is first[l] + its place among the codes of that length */

static Huff g_huff[12]; /* spectrum books 1..11 at 0..10, the scale factors at 11 */
static float g_pow43[8192 + 16]; /* (a pulse adds up to 15 to a value of up to 8191) */
static float g_win_long[2][1024], g_win_short[2][128]; /* rising halves: sine and Kaiser-Bessel derived */
static int g_ready;

typedef struct {
    int n;
    float *tw;       /* cos, sin of 2 pi k / n */
    unsigned short *rev;
} Fft;
static Fft g_fft[2]; /* 512 (long blocks) and 64 (short ones) points */
static float g_tw_pre[2][512 * 2];   /* the twiddle factors of the transform, per size: exp(-i pi (j + 1/8) / M) */

static void huff_build(Huff *h, const unsigned char *len, const unsigned *code, int n)
{
    memset(h, 0, sizeof(*h));
    int at = 0;
    for (int i = 0; i < n; i++)
        if (len[i] > h->maxlen)
            h->maxlen = len[i];
    for (int l = 1; l <= h->maxlen; l++) {
        h->offset[l] = (unsigned short)at;
        for (int i = 0; i < n; i++)
            if (len[i] == l)
                h->sym[at++] = (unsigned short)i;
        /* the symbols of this length in order of their codes (insertion sort) */
        for (int i = h->offset[l] + 1; i < at; i++) {
            unsigned short s = h->sym[i];
            int j = i - 1;
            while (j >= h->offset[l] && code[h->sym[j]] > code[s]) {
                h->sym[j + 1] = h->sym[j];
                j--;
            }
            h->sym[j + 1] = s;
        }
        h->count[l] = (unsigned short)(at - h->offset[l]);
        h->first[l] = h->count[l] ? code[h->sym[h->offset[l]]] : 0;
    }
    for (int i = 0; i < n; i++) {
        int l = len[i];
        if (l > 9)
            continue;
        unsigned lo = code[i] << (9 - l);
        for (unsigned k = 0; k < (1u << (9 - l)); k++)
            h->lut[lo + k] = (unsigned short)(i | (l << 9));
    }
}

static int huff_get(const Huff *h, Bits *b)
{
    unsigned e = h->lut[peek(b, 9)];
    if (e) {
        b->pos += e >> 9;
        return (int)(e & 511);
    }
    unsigned v = peek(b, h->maxlen);
    for (int l = 1; l <= h->maxlen; l++) {
        unsigned code = v >> (h->maxlen - l);
        if (h->count[l] && code >= h->first[l] && code - h->first[l] < h->count[l]) {
            b->pos += (unsigned)l;
            return h->sym[h->offset[l] + (code - h->first[l])];
        }
    }
    b->pos = b->end + 1; /* not a code: the frame is over */
    return 0;
}

static void fft_init(Fft *f, int n)
{
    f->n = n;
    f->tw = malloc(sizeof(float) * (size_t)(3 * n / 2 + 2)); /* w_n^k for k = 0 .. 3n/4 (the radix-4 passes use the cube of the twiddle factor) */
    f->rev = malloc(sizeof(unsigned short) * (size_t)n);
    for (int k = 0; k <= 3 * n / 4; k++) {
        f->tw[2 * k] = (float)cos(2.0 * M_PI * k / n);
        f->tw[2 * k + 1] = (float)-sin(2.0 * M_PI * k / n);
    }
    int bits = 0;
    while ((1 << bits) < n)
        bits++;
    for (int i = 0; i < n; i++) {
        int r = 0;
        for (int b = 0; b < bits; b++)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        f->rev[i] = (unsigned short)r;
    }
}

/* complex FFT in place; z holds n pairs (re, im) in bit reversed order (imdct() writes them so); the first two stages (twiddle factors 1 and -i) are done together */
static void fft_run(const Fft *f, float *z)
{
    int n = f->n;
    for (int i = 0; i < n; i += 4) {
        float *a = z + 2 * i;
        float s0r = a[0] + a[2], s0i = a[1] + a[3], d0r = a[0] - a[2], d0i = a[1] - a[3];
        float s1r = a[4] + a[6], s1i = a[5] + a[7], d1r = a[4] - a[6], d1i = a[5] - a[7];
        a[0] = s0r + s1r;
        a[1] = s0i + s1i;
        a[4] = s0r - s1r;
        a[5] = s0i - s1i;
        a[2] = d0r + d1i;
        a[3] = d0i - d1r;
        a[6] = d0r - d1i;
        a[7] = d0i + d1r;
    }
    /* radix-4 passes: four blocks of m points (each a finished transform) make one of 4m, two radix-2 stages at once: with W = w_{4m}^j, A = E0, B = E1 W^2, C = E2 W, D = E3 W^3 the results for the
     * points j, j + m, j + 2m, j + 3m are (A+B) + (C+D), (A-B) - i (C-D), (A+B) - (C+D), (A-B) + i (C-D). Half the loads and stores of two radix-2 stages, three complex products for four points. */
    int len = 4;
    for (; len * 4 <= n; len *= 4) {
        int m = len, q2 = 2 * (n / (4 * m)); /* the table index (floats) of w_{4m}^j grows by q2 with j */
        for (int base = 0; base < n; base += 4 * m) {
            float *e0 = z + 2 * base, *e1 = e0 + 2 * m, *e2 = e1 + 2 * m, *e3 = e2 + 2 * m;
            const float *t1 = f->tw, *t2 = f->tw, *t3 = f->tw;
            for (int j = 0; j < m; j++, t1 += q2, t2 += 2 * q2, t3 += 3 * q2, e0 += 2, e1 += 2, e2 += 2, e3 += 2) {
                float ar = e0[0], ai = e0[1];
                float w2r = t2[0], w2i = t2[1], w1r = t1[0], w1i = t1[1], w3r = t3[0], w3i = t3[1];
                float br = e1[0] * w2r - e1[1] * w2i, bi = e1[0] * w2i + e1[1] * w2r;
                float cr = e2[0] * w1r - e2[1] * w1i, ci = e2[0] * w1i + e2[1] * w1r;
                float dr = e3[0] * w3r - e3[1] * w3i, di = e3[0] * w3i + e3[1] * w3r;
                float s0r = ar + br, s0i = ai + bi, d0r = ar - br, d0i = ai - bi;
                float s1r = cr + dr, s1i = ci + di, d1r = cr - dr, d1i = ci - di;
                e0[0] = s0r + s1r;
                e0[1] = s0i + s1i;
                e2[0] = s0r - s1r;
                e2[1] = s0i - s1i;
                e1[0] = d0r + d1i;
                e1[1] = d0i - d1r;
                e3[0] = d0r - d1i;
                e3[1] = d0i + d1r;
            }
        }
    }
    if (len < n) { /* n is a power of two but not of four: one radix-2 stage is left (blocks of len = n / 2 make one of n) */
        int half = len, step = 2 * (n / (2 * len));
        float *a = z, *b = z + 2 * half;
        const float *w = f->tw;
        for (int j = 0; j < half; j++, w += step, a += 2, b += 2) {
            float wr = w[0], wi = w[1];
            float tr = b[0] * wr - b[1] * wi, ti = b[0] * wi + b[1] * wr;
            b[0] = a[0] - tr;
            b[1] = a[1] - ti;
            a[0] += tr;
            a[1] += ti;
        }
    }
}

static double bessel_i0(double x)
{
    double sum = 1, term = 1;
    for (int k = 1; k < 60; k++) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

/* the Kaiser-Bessel derived window: the rising half of a window of 2*m samples */
static void kbd_window(float *w, int m, double alpha)
{
    double *k = malloc(sizeof(double) * (size_t)(m + 1));
    double sum = 0;
    for (int n = 0; n <= m; n++) {
        double t = ((double)n - m / 2.0) / (m / 2.0);
        k[n] = bessel_i0(M_PI * alpha * sqrt(1.0 - t * t));
        sum += k[n];
    }
    double acc = 0;
    for (int n = 0; n < m; n++) {
        acc += k[n];
        w[n] = (float)sqrt(acc / sum);
    }
    free(k);
}

static void tables_init(void)
{
    if (g_ready)
        return;
    static const unsigned char *LEN[12] = { aac_sp1_len, aac_sp2_len, aac_sp3_len, aac_sp4_len, aac_sp5_len, aac_sp6_len, aac_sp7_len, aac_sp8_len, aac_sp9_len, aac_sp10_len, aac_sp11_len, aac_scf_len };
    static const unsigned *CODE[12] = { aac_sp1_code, aac_sp2_code, aac_sp3_code, aac_sp4_code, aac_sp5_code, aac_sp6_code, aac_sp7_code, aac_sp8_code, aac_sp9_code, aac_sp10_code, aac_sp11_code, aac_scf_code };
    static const int N[12] = { 81, 81, 81, 81, 81, 81, 64, 64, 169, 169, 289, 121 };
    for (int i = 0; i < 12; i++)
        huff_build(&g_huff[i], LEN[i], CODE[i], N[i]);
    for (int i = 0; i < 8192 + 16; i++)
        g_pow43[i] = (float)pow((double)i, 4.0 / 3.0);
    for (int n = 0; n < 1024; n++) {
        g_win_long[0][n] = (float)sin(M_PI * (n + 0.5) / 2048.0);
    }
    for (int n = 0; n < 128; n++)
        g_win_short[0][n] = (float)sin(M_PI * (n + 0.5) / 256.0);
    kbd_window(g_win_long[1], 1024, 4.0);
    kbd_window(g_win_short[1], 128, 6.0);
    fft_init(&g_fft[0], 512);
    fft_init(&g_fft[1], 64);
    for (int s = 0; s < 2; s++) {
        int m = s ? 128 : 1024;
        for (int j = 0; j < m / 2; j++) {
            g_tw_pre[s][2 * j] = (float)cos(M_PI * (j + 0.125) / m);
            g_tw_pre[s][2 * j + 1] = (float)-sin(M_PI * (j + 0.125) / m);
        }
    }
    g_ready = 1;
}

/* the sampling frequencies of the config, and the scale factor band tables that belong to them */
static const int RATE[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };

typedef struct {
    const short *lng, *sht;
    int nlong, nshort; /* the bands (the tables have one more entry) */
} Swb;

static int swb_for(int idx, Swb *s)
{
#define SW(L, S) do { s->lng = aac_swb_long_##L; s->sht = aac_swb_short_##S; s->nlong = (int)(sizeof(aac_swb_long_##L) / 2) - 1; s->nshort = (int)(sizeof(aac_swb_short_##S) / 2) - 1; } while (0)
    switch (idx) {
    case 0: case 1: SW(96, 64); break;
    case 2: SW(64, 64); break;
    case 3: case 4: SW(48, 48); break;
    case 5: SW(32, 48); break;
    case 6: case 7: SW(24, 24); break;
    case 8: case 9: case 10: SW(16, 16); break;
    case 11: case 12: SW(8, 8); break;
    default: return 0;
    }
#undef SW
    return 1;
}

static const unsigned char TNS_MAX_LONG[13] = { 31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39, 39 };
static const unsigned char TNS_MAX_SHORT[13] = { 9, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14 };

/* ---------------------------------------------------------------------- */
/* the decoder */

typedef struct {
    int seq, shape, max_sfb, n_groups, glen[MAXW], n_win; /* ics_info */
} Ics;

typedef struct {
    Ics ics;
    unsigned char cb[MAXW][MAXB];
    short sf[MAXW][MAXB];    /* per group: the scale factor (the noise energy, the intensity position) */
    short q[MAXW * 128];      /* the quantized spectrum (windows one after the other for short blocks) */
    float coef[MAXW * 128];
    int tns_present, n_filt[MAXW];
    struct { int length, order, dir; float lpc[13]; } tns[MAXW][3];
    int pulse_present, n_pulse, pulse_sfb, pulse_off[4], pulse_amp[4];
} Chan;

typedef struct {
    float overlap[1024]; /* the second half of the last block of a channel: it is added to the first half of the next */
    int prev_shape;
} Hist;

/* The channel configurations 1 to 7 of an AudioSpecificConfig: the elements a frame has, in this order (0 = a single channel, 1 = a pair, 3 = the low-frequency channel), and the place of each
 * channel they carry, in the order of the elements. (0, a layout in the stream itself, is not supported.) */
static const struct {
    unsigned char n_el, el[5], n_ch, role[AAC_MAX_CH];
} CFG[8] = {
    { 0, { 0 }, 0, { 0 } },
    { 1, { 0 }, 1, { DM_FC } },
    { 1, { 1 }, 2, { DM_FL, DM_FR } },
    { 2, { 0, 1 }, 3, { DM_FC, DM_FL, DM_FR } },
    { 3, { 0, 1, 0 }, 4, { DM_FC, DM_FL, DM_FR, DM_BC } },
    { 3, { 0, 1, 1 }, 5, { DM_FC, DM_FL, DM_FR, DM_BL, DM_BR } },
    { 4, { 0, 1, 1, 3 }, 6, { DM_FC, DM_FL, DM_FR, DM_BL, DM_BR, DM_LFE } },
    { 5, { 0, 1, 1, 1, 3 }, 8, { DM_FC, DM_FL, DM_FR, DM_SL, DM_SR, DM_BL, DM_BR, DM_LFE } }, /* "7.1": centre, front pair, side pair, back pair (ffmpeg's reading of it, and VLC's) */
};

struct AacDec {
    int rate_idx, rate, channels, config; /* config: the channel configuration of the stream */
    Swb swb;
    Chan ch[2];              /* the channels of the element that is decoded */
    unsigned random;
    float tmp[2048 + 2048];  /* the transform's output and its working space */
    float win[2048];         /* the windowed output of a block */
    float out[1024];         /* the sound of the channel that was just made */
    float mix[2][1024];      /* the left and right output of a frame: every channel, mixed in */
    float wl[AAC_MAX_CH], wr[AAC_MAX_CH]; /* how much of each channel (in the order of CFG[].role) goes to the left and right output */
    unsigned char fbuf[8192 + 16]; /* the frame, with zeros after it */
    Hist hist[1];            /* one for each channel (more are allocated after the structure) */
};

void aac_flush(AacDec *d)
{
    for (int i = 0; i < d->channels; i++) {
        memset(d->hist[i].overlap, 0, sizeof(d->hist[i].overlap));
        d->hist[i].prev_shape = 0;
    }
}

AacDec *aac_new(const unsigned char *cfg, int cfg_len, int *rate, int *channels)
{
    if (!cfg || cfg_len < 2)
        return NULL;
    Bits b = { cfg, 0, (unsigned)cfg_len * 8 };
    unsigned char pad[16] = { 0 };
    memcpy(pad, cfg, cfg_len > 12 ? 12 : (size_t)cfg_len);
    b.p = pad;
    int aot = (int)getb(&b, 5);
    if (aot == 31)
        aot = 32 + (int)getb(&b, 6);
    int idx = (int)getb(&b, 4), r;
    if (idx == 15)
        r = (int)getb(&b, 24);
    else if (idx < 13)
        r = RATE[idx];
    else
        return NULL;
    int chans = (int)getb(&b, 4);
    if (aot != 2 || chans < 1 || chans > 7 || idx > 12)
        return NULL; /* low complexity, channel configurations 1 to 7 (0 = a layout in the stream: no) */
    if (getb(&b, 1)) /* frameLengthFlag: 960-sample frames */
        return NULL;
    tables_init();
    int n_ch = CFG[chans].n_ch;
    AacDec *d = calloc(1, sizeof(*d) + (size_t)(n_ch - 1) * sizeof(Hist));
    if (!d)
        return NULL;
    d->rate_idx = idx;
    d->rate = r;
    d->config = chans;
    d->channels = n_ch;
    dm_weights(CFG[chans].role, n_ch, d->wl, d->wr);
    d->random = 0x1f2e3d4c;
    if (!swb_for(idx, &d->swb)) {
        free(d);
        return NULL;
    }
    if (rate)
        *rate = r;
    if (channels)
        *channels = n_ch;
    return d;
}

void aac_free(AacDec *d) { free(d); }

int aac_error_line(void) { return g_err_line; }

static int read_ics_info(AacDec *d, Bits *b, Ics *ics)
{
    if (get1(b))
        return BAD();
    ics->seq = (int)getb(b, 2);
    ics->shape = get1(b);
    if (ics->seq == EIGHT_SHORT) {
        ics->max_sfb = (int)getb(b, 4);
        unsigned grouping = getb(b, 7);
        ics->n_groups = 1;
        ics->glen[0] = 1;
        for (int i = 0; i < 7; i++) {
            if ((grouping >> (6 - i)) & 1)
                ics->glen[ics->n_groups - 1]++;
            else
                ics->glen[ics->n_groups++] = 1;
        }
        ics->n_win = 8;
        return ics->max_sfb <= d->swb.nshort;
    }
    ics->max_sfb = (int)getb(b, 6);
    ics->n_groups = 1;
    ics->glen[0] = 1;
    ics->n_win = 1;
    if (get1(b)) /* predictor_data_present: the Main profile's tool */
        return BAD();
    return ics->max_sfb <= d->swb.nlong;
}

static int read_section_data(Bits *b, Chan *c)
{
    const Ics *ics = &c->ics;
    int bits = ics->seq == EIGHT_SHORT ? 3 : 5, esc = (1 << bits) - 1;
    for (int g = 0; g < ics->n_groups; g++) {
        int k = 0;
        while (k < ics->max_sfb) {
            int cb = (int)getb(b, 4);
            if (cb == 12)
                return BAD();
            int len = 0, inc;
            while ((inc = (int)getb(b, bits)) == esc) {
                len += esc;
                if (b->pos > b->end)
                    return BAD();
            }
            len += inc;
            if (len == 0 || k + len > ics->max_sfb)
                return BAD();
            for (int i = 0; i < len; i++)
                c->cb[g][k + i] = (unsigned char)cb;
            k += len;
        }
        for (int k2 = ics->max_sfb; k2 < MAXB; k2++)
            c->cb[g][k2] = 0;
    }
    return 1;
}

static int read_scalefactors(Bits *b, Chan *c, int global_gain)
{
    const Ics *ics = &c->ics;
    int sf = global_gain, noise = global_gain - 90, is_pos = 0, noise_first = 1;
    const Huff *h = &g_huff[11];
    for (int g = 0; g < ics->n_groups; g++) {
        for (int s = 0; s < ics->max_sfb; s++) {
            int cb = c->cb[g][s];
            if (cb == ZERO_HCB) {
                c->sf[g][s] = 0;
            } else if (cb == INTENSITY_HCB || cb == INTENSITY_HCB2) {
                is_pos += huff_get(h, b) - 60;
                if (is_pos < -155) is_pos = -155;
                if (is_pos > 100) is_pos = 100;
                c->sf[g][s] = (short)is_pos;
            } else if (cb == NOISE_HCB) {
                if (noise_first) {
                    noise_first = 0;
                    noise += (int)getb(b, 9) - 256;
                } else {
                    noise += huff_get(h, b) - 60;
                }
                c->sf[g][s] = (short)noise;
            } else {
                sf += huff_get(h, b) - 60;
                if (sf < 0 || sf > 255)
                    return BAD();
                c->sf[g][s] = (short)sf;
            }
            if (b->pos > b->end)
                return BAD();
        }
    }
    return 1;
}

static int read_tns(const AacDec *d, Bits *b, Chan *c)
{
    const Ics *ics = &c->ics;
    int lng = ics->seq != EIGHT_SHORT;
    int max_order = lng ? 12 : 7;
    for (int w = 0; w < ics->n_win; w++) {
        c->n_filt[w] = (int)getb(b, lng ? 2 : 1);
        if (!c->n_filt[w])
            continue;
        int res = get1(b);
        for (int f = 0; f < c->n_filt[w]; f++) {
            int length = (int)getb(b, lng ? 6 : 4), order = (int)getb(b, lng ? 5 : 3);
            if (order > max_order)
                return BAD();
            c->tns[w][f].length = length;
            c->tns[w][f].order = order;
            c->tns[w][f].dir = 0;
            if (!order)
                continue;
            c->tns[w][f].dir = get1(b);
            int compress = get1(b), bits = (res ? 4 : 3) - compress;
            float base = res ? 8.0f : 4.0f;
            float iq = (base - 0.5f) / (float)(M_PI / 2), iqm = (base + 0.5f) / (float)(M_PI / 2);
            float parcor[12];
            for (int i = 0; i < order; i++) {
                int v = (int)getb(b, bits);
                if (v & (1 << (bits - 1)))
                    v -= 1 << bits;
                parcor[i] = sinf(v >= 0 ? (float)v / iq : (float)v / iqm);
            }
            /* the filter coefficients (the LPC) from the reflection coefficients */
            float lpc[13] = { 0 }, tmp[13];
            for (int m = 1; m <= order; m++) {
                for (int i = 1; i < m; i++)
                    tmp[i] = lpc[i - 1] + parcor[m - 1] * lpc[m - i - 1];
                for (int i = 1; i < m; i++)
                    lpc[i - 1] = tmp[i];
                lpc[m - 1] = parcor[m - 1];
            }
            memcpy(c->tns[w][f].lpc, lpc, sizeof(lpc));
        }
    }
    (void)d;
    return 1;
}

static int read_escape(Bits *b)
{
    int n = 4;
    while (get1(b)) {
        if (++n > 12 || b->pos > b->end)
            return -1;
    }
    return (1 << n) + (int)getb(b, n);
}

/* the values of one band of a section (width of them) in q[0..width) with codebook cb; 0 when the frame is damaged */
static int read_band(Bits *b, int cb, short *q, int width)
{
    const Huff *h = &g_huff[cb - 1];
    if (cb <= 4) { /* quadruples */
        for (int i = 0; i < width; i += 4) {
            int idx = huff_get(h, b), v[4];
            if (cb <= 2) {
                v[0] = idx / 27 - 1; v[1] = idx / 9 % 3 - 1; v[2] = idx / 3 % 3 - 1; v[3] = idx % 3 - 1;
            } else {
                v[0] = idx / 27; v[1] = idx / 9 % 3; v[2] = idx / 3 % 3; v[3] = idx % 3;
                for (int k = 0; k < 4; k++)
                    if (v[k] && get1(b))
                        v[k] = -v[k];
            }
            for (int k = 0; k < 4; k++)
                q[i + k] = (short)v[k];
            if (b->pos > b->end)
                return BAD();
        }
        return 1;
    }
    for (int i = 0; i < width; i += 2) { /* pairs */
        int idx = huff_get(h, b), x, y;
        switch (cb) {
        case 5: case 6: x = idx / 9 - 4; y = idx % 9 - 4; break;
        case 7: case 8: x = idx >> 3; y = idx & 7; break;
        case 9: case 10: x = idx / 13; y = idx % 13; break;
        default: x = idx / 17; y = idx % 17; break;
        }
        if (cb >= 7) {
            int sx = x && get1(b), sy = y && get1(b);
            if (cb == ESC_HCB) {
                if (x == 16 && (x = read_escape(b)) < 0)
                    return BAD();
                if (y == 16 && (y = read_escape(b)) < 0)
                    return BAD();
            }
            if (sx) x = -x;
            if (sy) y = -y;
        }
        q[i] = (short)x;
        q[i + 1] = (short)y;
        if (b->pos > b->end)
            return BAD();
    }
    return 1;
}

static int read_spectrum(const AacDec *d, Bits *b, Chan *c)
{
    const Ics *ics = &c->ics;
    const short *swb = ics->seq == EIGHT_SHORT ? d->swb.sht : d->swb.lng;
    memset(c->q, 0, sizeof(c->q));
    int win0 = 0;
    for (int g = 0; g < ics->n_groups; g++) {
        for (int s = 0; s < ics->max_sfb; s++) {
            int cb = c->cb[g][s], width = swb[s + 1] - swb[s];
            if (cb == ZERO_HCB || cb >= NOISE_HCB)
                continue;
            for (int w = 0; w < ics->glen[g]; w++)
                if (!read_band(b, cb, c->q + (win0 + w) * 128 + swb[s], width))
                    return BAD();
        }
        win0 += ics->glen[g];
    }
    return 1;
}

static float gain_of(int e) /* 2^(e/4) */
{
    static const float F[4] = { 1.0f, 1.18920712f, 1.41421356f, 1.68179283f };
    return ldexpf(F[e & 3], e >> 2);
}

/* the spectrum of the channel: the quantized values with the scale factors, and the noise of the noise bands */
static void dequant(AacDec *d, Chan *c)
{
    const Ics *ics = &c->ics;
    const short *swb = ics->seq == EIGHT_SHORT ? d->swb.sht : d->swb.lng;
    memset(c->coef, 0, sizeof(c->coef));
    if (c->pulse_present && ics->seq != EIGHT_SHORT && c->pulse_sfb < d->swb.nlong) {
        int k = swb[c->pulse_sfb];
        for (int i = 0; i < c->n_pulse; i++) {
            k += c->pulse_off[i];
            if (k >= 1024)
                break;
            if (c->q[k] > 0)
                c->q[k] = (short)(c->q[k] + c->pulse_amp[i]);
            else
                c->q[k] = (short)(c->q[k] - c->pulse_amp[i]);
        }
    }
    int win0 = 0;
    for (int g = 0; g < ics->n_groups; g++) {
        for (int s = 0; s < ics->max_sfb; s++) {
            int cb = c->cb[g][s], width = swb[s + 1] - swb[s];
            if (cb == ZERO_HCB || cb > NOISE_HCB)
                continue;
            for (int w = 0; w < ics->glen[g]; w++) {
                float *out = c->coef + (win0 + w) * 128 + swb[s];
                if (cb == NOISE_HCB) {
                    float energy = 0;
                    for (int k = 0; k < width; k++) {
                        d->random = d->random * 1664525u + 1013904223u;
                        out[k] = (float)(int)d->random;
                        energy += out[k] * out[k];
                    }
                    float scale = energy > 0 ? gain_of(c->sf[g][s]) / sqrtf(energy) : 0;
                    for (int k = 0; k < width; k++)
                        out[k] *= scale;
                } else {
                    const short *q = c->q + (win0 + w) * 128 + swb[s];
                    float gain = gain_of(c->sf[g][s] - 100);
                    for (int k = 0; k < width; k++) {
                        int v = q[k];
                        out[k] = v < 0 ? -g_pow43[-v] * gain : g_pow43[v] * gain;
                    }
                }
            }
        }
        win0 += ics->glen[g];
    }
}

static void apply_tns(const AacDec *d, Chan *c)
{
    const Ics *ics = &c->ics;
    int lng = ics->seq != EIGHT_SHORT;
    const short *swb = lng ? d->swb.lng : d->swb.sht;
    int nb = lng ? d->swb.nlong : d->swb.nshort;
    int maxb = lng ? TNS_MAX_LONG[d->rate_idx] : TNS_MAX_SHORT[d->rate_idx];
    if (maxb > ics->max_sfb)
        maxb = ics->max_sfb;
    for (int w = 0; w < ics->n_win; w++) {
        int bottom = nb;
        for (int f = 0; f < c->n_filt[w]; f++) {
            int top = bottom;
            bottom = top - c->tns[w][f].length;
            if (bottom < 0)
                bottom = 0;
            int order = c->tns[w][f].order;
            if (!order)
                continue;
            int start = w * 128 + swb[bottom < maxb ? bottom : maxb], end = w * 128 + swb[top < maxb ? top : maxb];
            const float *lpc = c->tns[w][f].lpc;
            float *x = c->coef;
            if (!c->tns[w][f].dir) {
                for (int m = 0, i = start; i < end; i++, m++)
                    for (int j = 0; j < order && j < m; j++)
                        x[i] -= x[i - j - 1] * lpc[j];
            } else {
                for (int m = 0, i = end - 1; i >= start; i--, m++)
                    for (int j = 0; j < order && j < m; j++)
                        x[i] -= x[i + j + 1] * lpc[j];
            }
        }
    }
}

/* ---------------------------------------------------------------------- */
/* the channel elements */

static int read_ics(AacDec *d, Bits *b, Chan *c, int common, const Ics *shared)
{
    int gain = (int)getb(b, 8);
    if (common)
        c->ics = *shared;
    else if (!read_ics_info(d, b, &c->ics))
        return BAD();
    if (!read_section_data(b, c) || !read_scalefactors(b, c, gain))
        return BAD();
    c->pulse_present = get1(b);
    if (c->pulse_present) {
        c->n_pulse = (int)getb(b, 2) + 1;
        c->pulse_sfb = (int)getb(b, 6);
        for (int i = 0; i < c->n_pulse; i++) {
            c->pulse_off[i] = (int)getb(b, 5);
            c->pulse_amp[i] = (int)getb(b, 4);
        }
        if (c->ics.seq == EIGHT_SHORT)
            return BAD();
    }
    memset(c->n_filt, 0, sizeof(c->n_filt));
    c->tns_present = get1(b);
    if (c->tns_present && !read_tns(d, b, c))
        return BAD();
    if (get1(b)) /* gain control: the scalable profile's tool */
        return BAD();
    return read_spectrum(d, b, c) && b->pos <= b->end;
}

/* mid/side and intensity stereo on the two spectra of a pair */
static void stereo_tools(const AacDec *d, Chan *l, Chan *r, int ms_present, const unsigned char ms[MAXW][MAXB])
{
    const Ics *ics = &l->ics;
    const short *swb = ics->seq == EIGHT_SHORT ? d->swb.sht : d->swb.lng;
    int win0 = 0;
    for (int g = 0; g < ics->n_groups; g++) {
        for (int s = 0; s < ics->max_sfb; s++) {
            int width = swb[s + 1] - swb[s], cbr = r->cb[g][s];
            for (int w = 0; w < ics->glen[g]; w++) {
                float *a = l->coef + (win0 + w) * 128 + swb[s], *bb = r->coef + (win0 + w) * 128 + swb[s];
                if (cbr == INTENSITY_HCB || cbr == INTENSITY_HCB2) {
                    float scale = gain_of(-r->sf[g][s]);
                    if (cbr == INTENSITY_HCB2)
                        scale = -scale;
                    if (ms_present && ms[g][s])
                        scale = -scale;
                    for (int k = 0; k < width; k++)
                        bb[k] = a[k] * scale;
                } else if (ms_present && ms[g][s] && l->cb[g][s] < NOISE_HCB && cbr < NOISE_HCB) {
                    for (int k = 0; k < width; k++) {
                        float m = a[k], sd = bb[k];
                        a[k] = m + sd;
                        bb[k] = m - sd;
                    }
                }
            }
        }
        win0 += ics->glen[g];
    }
}

/* ---------------------------------------------------------------------- */
/* the filter bank */

/* The inverse MDCT of m coefficients into 2m samples (m = 1024 or 128), scaled by 1/m: the standard's definition, done as a DCT-IV with a complex FFT of m/2 points and then unfolded. */
static void imdct(const float *x, float *y, int m, float *work)
{
    int sel = m == 1024 ? 0 : 1;
    const float *tw = g_tw_pre[sel];
    int h = m / 2;
    const unsigned short *rev = g_fft[sel].rev;
    for (int j = 0; j < h; j++) {
        float re = x[2 * j], im = x[m - 1 - 2 * j];
        int r = rev[j];
        work[2 * r] = re * tw[2 * j] - im * tw[2 * j + 1];
        work[2 * r + 1] = re * tw[2 * j + 1] + im * tw[2 * j];
    }
    fft_run(&g_fft[sel], work);
    float scale = 1.0f / (float)m;
    float *u = work + 2 * h; /* the DCT-IV, m values */
    for (int p = 0; p < h; p++) {
        float re = work[2 * p] * tw[2 * p] - work[2 * p + 1] * tw[2 * p + 1];
        float im = work[2 * p] * tw[2 * p + 1] + work[2 * p + 1] * tw[2 * p];
        u[2 * p] = re * scale;
        u[m - 1 - 2 * p] = -im * scale;
    }
    int q = m / 2;
    for (int n = 0; n < q; n++)
        y[n] = u[n + q];
    for (int n = q; n < 3 * q; n++)
        y[n] = -u[3 * q - 1 - n];
    for (int n = 3 * q; n < 2 * m; n++)
        y[n] = -u[n - 3 * q];
}

/* one channel's frame of spectrum into 1024 samples of sound (float, 16-bit scale), with the overlap of the frame before */
static void synth(AacDec *d, Chan *c, Hist *h, float *out)
{
    const Ics *ics = &c->ics;
    float *work = d->tmp + 2048;
    float *buf = d->win;
    const float *lp = g_win_long[h->prev_shape], *lc = g_win_long[ics->shape];
    const float *sp = g_win_short[h->prev_shape], *sc = g_win_short[ics->shape];
    if (ics->seq == EIGHT_SHORT) {
        memset(buf, 0, sizeof(float) * 2048);
        for (int w = 0; w < 8; w++) {
            float *t = d->tmp;
            imdct(c->coef + w * 128, t, 128, work);
            const float *wl = w == 0 ? sp : sc;
            float *dst = buf + 448 + 128 * w;
            for (int n = 0; n < 128; n++) {
                dst[n] += t[n] * wl[n];
                dst[128 + n] += t[128 + n] * sc[127 - n];
            }
        }
    } else {
        float *t = d->tmp;
        imdct(c->coef, t, 1024, work);
        if (ics->seq == LONG_STOP) {
            for (int n = 0; n < 448; n++)
                buf[n] = 0;
            for (int n = 0; n < 128; n++)
                buf[448 + n] = t[448 + n] * sp[n];
            for (int n = 576; n < 1024; n++)
                buf[n] = t[n];
        } else {
            for (int n = 0; n < 1024; n++)
                buf[n] = t[n] * lp[n];
        }
        if (ics->seq == LONG_START) {
            for (int n = 0; n < 448; n++)
                buf[1024 + n] = t[1024 + n];
            for (int n = 0; n < 128; n++)
                buf[1024 + 448 + n] = t[1024 + 448 + n] * sc[127 - n];
            for (int n = 576; n < 1024; n++)
                buf[1024 + n] = 0;
        } else {
            for (int n = 0; n < 1024; n++)
                buf[1024 + n] = t[1024 + n] * lc[1023 - n];
        }
    }
    for (int n = 0; n < 1024; n++) {
        out[n] = h->overlap[n] + buf[n];
        h->overlap[n] = buf[1024 + n];
    }
    h->prev_shape = ics->shape;
}

static short clip16(float v)
{
    v = v < 0 ? v - 0.5f : v + 0.5f;
    if (v > 32767.0f)
        return 32767;
    if (v < -32768.0f)
        return -32768;
    return (short)v;
}

/* ---------------------------------------------------------------------- */

/* the first channel (place in CFG[].role) of the k-th element of this kind (0 single, 1 pair, 3 low-frequency) that the configuration has, -1 when it has no more of them */
static int find_element(const AacDec *d, int kind, int k)
{
    int ch = 0;
    for (int i = 0; i < CFG[d->config].n_el; i++) {
        int e = CFG[d->config].el[i];
        if (e == kind && k-- == 0)
            return ch;
        ch += e == 1 ? 2 : 1;
    }
    return -1;
}

/* a channel whose spectrum is ready: into sound, and into the left and right output with its weights */
static void add_channel(AacDec *d, Chan *c, int hist, float wl, float wr)
{
    if (c->tns_present)
        apply_tns(d, c);
    synth(d, c, &d->hist[hist], d->out);
    if (wl != 0)
        for (int i = 0; i < AAC_FRAME; i++)
            d->mix[0][i] += d->out[i] * wl;
    if (wr != 0)
        for (int i = 0; i < AAC_FRAME; i++)
            d->mix[1][i] += d->out[i] * wr;
}

int aac_decode(AacDec *d, const unsigned char *frame, int len, short *pcm)
{
    unsigned char *buf = d->fbuf;
    int ok = 1, got = 0, made = 0, seen[4] = { 0, 0, 0, 0 }; /* got: an element came; made: a channel was made; seen: the elements of each kind that came */
    memset(pcm, 0, (size_t)AAC_FRAME * 4);
    if (len <= 0 || len > 8192)
        return BAD();
    memcpy(buf, frame, (size_t)len);
    memset(buf + len, 0, 16);
    Bits b = { buf, 0, (unsigned)len * 8 };
    Chan *ch = d->ch;
    memset(d->mix, 0, sizeof(d->mix));
    while (b.pos + 3 <= b.end && ok) {
        int id = (int)getb(&b, 3);
        if (id == 7) /* end */
            break;
        if (id == 0 || id == 3) { /* single channel or LFE */
            getb(&b, 4);
            int first = find_element(d, id, seen[id]++);
            float wl = 1, wr = 1;
            if (first >= 0) {
                wl = d->wl[first];
                wr = d->wr[first];
            } else if (d->config <= 2 && !got) {
                first = 0; /* a mono element in a stream that says stereo (or the reverse): played as it is, on both sides */
            } else {
                ok = BAD(); /* more elements than the configuration has */
                break;
            }
            ok = read_ics(d, &b, &ch[0], 0, NULL);
            if (ok && (id == 0 || first == 0)) { /* (the low-frequency channel is read and left out) */
                dequant(d, &ch[0]);
                add_channel(d, &ch[0], first, wl, wr);
                made = 1;
            }
            got = 1;
        } else if (id == 1) { /* channel pair */
            getb(&b, 4);
            int first = find_element(d, 1, seen[1]++);
            if (first < 0) {
                ok = BAD();
                break;
            }
            int common = get1(&b), ms_present = 0;
            unsigned char ms[MAXW][MAXB];
            memset(ms, 0, sizeof(ms));
            Ics shared;
            if (common) {
                ok = read_ics_info(d, &b, &shared);
                if (ok) {
                    ms_present = (int)getb(&b, 2);
                    if (ms_present == 3)
                        ok = BAD();
                    else if (ms_present == 1) {
                        for (int g = 0; g < shared.n_groups; g++)
                            for (int s = 0; s < shared.max_sfb; s++)
                                ms[g][s] = (unsigned char)get1(&b);
                    } else if (ms_present == 2) {
                        for (int g = 0; g < shared.n_groups; g++)
                            for (int s = 0; s < shared.max_sfb; s++)
                                ms[g][s] = 1;
                    }
                }
            }
            if (ok)
                ok = read_ics(d, &b, &ch[0], common, &shared) && read_ics(d, &b, &ch[1], common, &shared);
            if (ok) {
                dequant(d, &ch[0]);
                dequant(d, &ch[1]);
                if (common && ch[0].ics.seq == ch[1].ics.seq)
                    stereo_tools(d, &ch[0], &ch[1], ms_present != 0, ms);
                for (int i = 0; i < 2; i++)
                    add_channel(d, &ch[i], first + i, d->wl[first + i], d->wr[first + i]);
                made = 1;
            }
            got = 1;
        } else if (id == 4) { /* data stream element */
            getb(&b, 4);
            int align = get1(&b), cnt = (int)getb(&b, 8);
            if (cnt == 255)
                cnt += (int)getb(&b, 8);
            if (align)
                b.pos = (b.pos + 7) & ~7u;
            b.pos += (unsigned)cnt * 8;
        } else if (id == 6) { /* fill element */
            int cnt = (int)getb(&b, 4);
            if (cnt == 15)
                cnt += (int)getb(&b, 8) - 1;
            b.pos += (unsigned)cnt * 8;
        } else if (id == 5) { /* program config element: skipped */
            getb(&b, 4 + 2 + 4);
            int nf = (int)getb(&b, 4), ns = (int)getb(&b, 4), nb = (int)getb(&b, 4), nl = (int)getb(&b, 2), na = (int)getb(&b, 3), nc = (int)getb(&b, 4);
            if (get1(&b))
                getb(&b, 4);
            if (get1(&b))
                getb(&b, 4);
            if (get1(&b))
                getb(&b, 3);
            b.pos += (unsigned)(nf + ns + nb) * 5 + (unsigned)nl * 4 + (unsigned)na * 4 + (unsigned)nc * 5;
            b.pos = (b.pos + 7) & ~7u;
            b.pos += getb(&b, 8) * 8;
        } else { /* a coupling channel element, or nonsense */
            ok = BAD();
        }
        if (b.pos > b.end)
            ok = BAD();
    }
    if (!made)
        return BAD();
    for (int i = 0; i < AAC_FRAME; i++) {
        pcm[2 * i] = clip16(d->mix[0][i]);
        pcm[2 * i + 1] = clip16(d->mix[1][i]);
    }
    return ok;
}
