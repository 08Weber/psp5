/* psp5 authentic – a stand-in for the PSP's own decoder, for the tests in PPSSPP only (-DPSP5_HW_FAKE, never in a build that is shipped): hwavc.c includes this instead of its own code.
 *
 * PPSSPP has no Media Engine image to start and no VSH library, so the route of video.c (hw_step, hwpipe.c, the jumps, the end of the video) cannot run in it. This gives the same functions with the
 * software decoder's pictures, and makes them behave as the PSP's decoder was seen to: it is 3 samples behind (the first pictures come out at the 4th sample), sometimes gives nothing and then two pictures at
 * once, a key frame given while it holds pictures makes it put them out, and a picture is RGBA with 512 pixels a row. CAVLC streams without B pictures only (h264dec.c's); the order of the pictures is
 * then the order of the samples (what the pull scheduler makes of B pictures is tested by hwpipe_test.c). */
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "h264dec.h"

#define FK_DELAY 3
#define FK_W 512
#define FK_ROWS 288

static void (*s_log)(const char *, unsigned, unsigned, unsigned, unsigned);
void hwavc_log(void (*fn)(const char *, unsigned, unsigned, unsigned, unsigned)) { s_log = fn; }
int hwlib_up(void) { return 0; }
void hwlib_down(void) {}
int hwlib_is_up(void) { return 1; }

struct HwAvc {
    H264Dec *dec;
    uint32_t *q[5]; /* the pictures it holds (RGBA), the next to come out first */
    int qn;
    uint16_t *tmp;
    int calls, rows; /* the rows of a picture (a multiple of 16) */
    unsigned char *params;
    int params_len, nal_prefix;
};

static void fake_clear(HwAvc *h)
{
    h->qn = 0;
    h->calls = 0;
    h264d_reset(h->dec);
}

HwAvc *hwavc_open(const unsigned char *sps, int sps_len, const unsigned char *pps, int pps_len, int nal_prefix)
{
    if (!sps || !pps || sps_len <= 0 || pps_len <= 0 || nal_prefix < 1 || nal_prefix > 4)
        return NULL;
    HwAvc *h = calloc(1, sizeof(*h));
    if (!h)
        return NULL;
    h->params_len = 8 + sps_len + pps_len;
    h->params = malloc((size_t)h->params_len);
    h->tmp = malloc((size_t)FK_W * FK_ROWS * 2);
    for (int i = 0; i < 5; i++)
        h->q[i] = memalign(64, (size_t)FK_W * FK_ROWS * 4);
    if (!h->params || !h->tmp || !h->q[4]) {
        hwavc_close(h);
        return NULL;
    }
    static const unsigned char sc[4] = { 0, 0, 0, 1 };
    memcpy(h->params, sc, 4);
    memcpy(h->params + 4, sps, (size_t)sps_len);
    memcpy(h->params + 4 + sps_len, sc, 4);
    memcpy(h->params + 8 + sps_len, pps, (size_t)pps_len);
    h->nal_prefix = nal_prefix;
    int why = 0;
    h->dec = h264d_new(h->params, h->params_len, nal_prefix, &why);
    if (!h->dec) {
        hwavc_close(h);
        return NULL;
    }
    return h;
}

void hwavc_close(HwAvc *h)
{
    if (!h)
        return;
    h264d_free(h->dec);
    for (int i = 0; i < 5; i++)
        free(h->q[i]);
    free(h->tmp);
    free(h->params);
    free(h);
}

int hwavc_reset(HwAvc *h)
{
    fake_clear(h);
    return 0;
}

static void emit(HwAvc *h, int k, void *dst)
{
    if (dst)
        memcpy(dst, h->q[0], (size_t)FK_W * (size_t)h->rows * 4); /* (what the converter writes: whole rows of blocks) */
    uint32_t *first = h->q[0];
    for (int i = 1; i < h->qn; i++)
        h->q[i - 1] = h->q[i];
    h->q[--h->qn] = first;
    (void)k;
}

int hwavc_decode(HwAvc *h, void *sample, int size, int mode, void *rgb[HWAVC_PICS], int *pics, int *w, int *h_out, unsigned *err)
{
    *pics = 0;
    *err = 0;
    h->calls++;
    int n = 0;
    if (mode == 3 && h->qn > 0) { /* a key frame while pictures are held: they come out first (all that it holds, three on the PSP-1000's log: HWAVC_PICS at most in one call) */
        while (h->qn > 0 && n < HWAVC_PICS) {
            emit(h, n, rgb ? rgb[n] : NULL);
            n++;
        }
    }
    int r = h264d_decode(h->dec, (const uint8_t *)sample, size);
    if (r < 0) {
        *err = 0x80628002u;
        return -2;
    }
    if (r > 0) {
        h264d_rgb565(h->dec, h->tmp, FK_W, 0);
        int ww = h264d_width(h->dec), hh = h264d_height(h->dec);
        h->rows = (hh + 15) & ~15;
        uint32_t *o = h->q[h->qn];
        for (int y = 0; y < hh; y++)
            for (int x = 0; x < ww; x++) {
                unsigned p = h->tmp[y * FK_W + x], rr = p & 31, gg = (p >> 5) & 63, bb = p >> 11;
                o[y * FK_W + x] = 0xFF000000u | ((bb << 3 | bb >> 2) << 16) | ((gg << 2 | gg >> 4) << 8) | (rr << 3 | rr >> 2);
            }
        h->qn++;
    }
    if (n == 0) { /* the decoder is FK_DELAY samples behind; now and then it gives nothing and so two at the next call */
        int burst = h->calls % 17 == 5;
        int want = h->qn - FK_DELAY;
        if (burst)
            want = 0;
        else if (want > 2)
            want = 2;
        while (want-- > 0 && h->qn > 0) {
            emit(h, n, rgb ? rgb[n] : NULL);
            n++;
        }
    }
    if (n >= 3 && s_log) /* (the line that the real library's code writes for it) */
        s_log("hwd-burst", (unsigned)n, (unsigned)mode, (unsigned)h->calls, 0);
    *w = h264d_width(h->dec);
    *h_out = h264d_height(h->dec);
    *pics = n;
    return 0;
}
