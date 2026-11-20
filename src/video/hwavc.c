/* psp5 authentic – the PSP's own H.264 decoder for a CAVLC stream (see hwavc.h) */
#include "hwavc.h"

#ifdef PSP5_HW_FAKE /* (a test build for PPSSPP: a stand-in with the software decoder's pictures, see hwfake.inc) */
#include "hwfake.inc"
#else

#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <pspkernel.h>
#include <pspmpeg.h>
#include <psputility.h>
#include <psputility_avmodules.h>
#include <kubridge.h>

extern int sceMpegGetAvcNalAu(SceMpeg *mpeg, void *nal, SceMpegAu *au); /* mpegvsh.S */
extern int sceMpegAvcDecodeDetail2(SceMpeg *mpeg, void *detail);       /* (in the SDK's library, not in its headers) */
extern int sceMpegBaseCscAvc(void *destination, int unknown, int width, void *csc);

#define MODE 4          /* the mode of the library for a stream of up to 480 x 272 */
#define FRAME_W 512     /* the width of a row of the picture the library writes, in pixels */
#define DDR_ALIGN 0x400000
#define DDR_SIZE 0x200000
#define AU_OFFSET 0x10000
#define ALREADY_LOADED 0x80111102u    /* what sceUtilityLoadModule says of a module that is loaded */
#define AV_ALREADY_LOADED 0x80110F02u /* ... and sceUtilityLoadAvModule (the same series as 0x80111101 / 02 / 03: 0x80110F01 bad id, 02 loaded, 03 not loaded; the first PSP-1000 run of this route gave it, psp5 had loaded the codecs with the other call) */
#define VSH_PATH "flash0:/kd/mpeg_vsh.prx"

static void (*s_log)(const char *, unsigned, unsigned, unsigned, unsigned);
void hwavc_log(void (*fn)(const char *, unsigned, unsigned, unsigned, unsigned)) { s_log = fn; }
static void L(const char *tag, unsigned a, unsigned b, unsigned c, unsigned d)
{
    if (s_log)
        s_log(tag, a, b, c, d);
}

/* ---------------------------------------------------------------------- */
/* the library */

static SceUID s_vsh = -1;
static int s_started, s_av_owned;

int hwlib_is_up(void) { return s_vsh >= 0 && s_started; }

int hwlib_up(void)
{
    if (hwlib_is_up())
        return 0;
    int r = sceUtilityLoadAvModule(PSP_AV_MODULE_AVCODEC);
    L("hwl-av", (unsigned)r, 0, 0, 0);
    if (r < 0 && (unsigned)r != ALREADY_LOADED && (unsigned)r != AV_ALREADY_LOADED)
        return -1;
    s_av_owned = r >= 0; /* (another answer: somebody else holds it, it is not ours to unload) */
    s_vsh = kuKernelLoadModule(VSH_PATH, 0, NULL);
    L("hwl-load", (unsigned)s_vsh, 0, 0, 0);
    if (s_vsh < 0) {
        s_vsh = -1;
        if (s_av_owned) {
            sceUtilityUnloadAvModule(PSP_AV_MODULE_AVCODEC);
            s_av_owned = 0;
        }
        return -2;
    }
    int status = 0;
    r = sceKernelStartModule(s_vsh, 0, NULL, &status, NULL);
    L("hwl-start", (unsigned)r, (unsigned)status, 0, 0);
    if (r < 0 || status < 0) {
        sceKernelUnloadModule(s_vsh);
        s_vsh = -1;
        if (s_av_owned) {
            sceUtilityUnloadAvModule(PSP_AV_MODULE_AVCODEC);
            s_av_owned = 0;
        }
        return -3;
    }
    s_started = 1;
    return 0;
}

void hwlib_down(void)
{
    if (s_vsh >= 0) {
        int status = 0;
        if (s_started) {
            int r = sceKernelStopModule(s_vsh, 0, NULL, &status, NULL);
            L("hwl-stop", (unsigned)r, (unsigned)status, 0, 0);
            if (r < 0 || status < 0)
                return; /* (it is not stopped: it stays loaded, nothing is unloaded under it) */
            s_started = 0;
        }
        int r = sceKernelUnloadModule(s_vsh);
        L("hwl-unload", (unsigned)r, 0, 0, 0);
        if (r < 0)
            return;
        s_vsh = -1;
    }
    if (s_av_owned) {
        sceUtilityUnloadAvModule(PSP_AV_MODULE_AVCODEC);
        s_av_owned = 0;
    }
}

/* ---------------------------------------------------------------------- */
/* the decoder: structures as the library wants them (the fields that are not named are not known) */

typedef struct { /* sceMpegGetAvcNalAu's argument */
    void *sps;
    int sps_size;
    void *pps;
    int pps_size;
    int nal_prefix;
    void *nal;
    int nal_size;
    int mode;
} NalArg;

typedef struct { /* what the picture is: its size is the 3rd and 4th number */
    int u0, u1, width, height, u4, u5, u6, u7, u8, u9;
} PicInfo;

typedef struct { /* the planes of the picture, as the library keeps them (eight buffers) */
    void *buf[8];
    int u0, u1, u2;
} PicYuv;

typedef struct { /* sceMpegAvcDecodeDetail2 gives a pointer to this: the 5th word is the picture's info, the 12th its planes */
    int u0, u1, u2, u3;
    PicInfo *info;
    int u5, u6, u7, u8, u9, u10;
    PicYuv *yuv;
    int u12[12];
} Detail2;

typedef struct { /* sceMpegBaseCscAvc's description of what it converts: in macroblocks of 16 */
    int height, width, mode0, mode1;
    void *buf[8];
} Csc;

static void *s_ddr; /* kept for as long as the program runs: the library may still address it after a video is closed */

struct HwAvc {
    SceMpeg mpeg;
    SceMpegAu *au;
    void *mem;
    unsigned char *ps; /* the SPS, then the PPS */
    int sps_len, pps_len, nal_prefix;
    int inited, created;
    unsigned char ring[96] __attribute__((aligned(16))); /* (the library is given a structure of a ring buffer; this one is never made) */
    int calls;
};

/* the library's own state: Init, the memory, Create, InitAu. 0 when it is all up; else what was made is given back */
static int session_open(HwAvc *h)
{
    int r = sceMpegInit();
    L("hwo-init", (unsigned)r, 0, 0, 0);
    if (r != 0)
        return -1;
    h->inited = 1;
    int size = sceMpegQueryMemSize(MODE);
    L("hwo-size", (unsigned)size, 0, 0, 0);
    if (size <= 0)
        return -2;
    h->mem = memalign(64, (size_t)(size + 63) & ~63u);
    if (!h->mem)
        return -2;
    memset(h->ring, 0, sizeof(h->ring));
    r = sceMpegCreate(&h->mpeg, h->mem, size, (SceMpegRingbuffer *)h->ring, FRAME_W, MODE, (SceInt32)(uintptr_t)s_ddr);
    L("hwo-create", (unsigned)r, (unsigned)size, (unsigned)MODE, (unsigned)(uintptr_t)s_ddr);
    if (r != 0)
        return -3;
    h->created = 1;
    memset(h->au, 0xFF, 64);
    r = sceMpegInitAu(&h->mpeg, (unsigned char *)s_ddr + AU_OFFSET, h->au);
    L("hwo-initau", (unsigned)r, 0, 0, 0);
    return r != 0 ? -4 : 0;
}

static void session_close(HwAvc *h)
{
    if (h->created) {
        sceMpegDelete(&h->mpeg);
        L("hwc-delete", 0, 0, 0, 0);
        h->created = 0;
    }
    if (h->inited) {
        sceMpegFinish();
        L("hwc-finish", 0, 0, 0, 0);
        h->inited = 0;
    }
    free(h->mem);
    h->mem = NULL;
}

HwAvc *hwavc_open(const unsigned char *sps, int sps_len, const unsigned char *pps, int pps_len, int nal_prefix)
{
    if (!sps || !pps || sps_len <= 0 || pps_len <= 0 || nal_prefix < 1 || nal_prefix > 4)
        return NULL;
    HwAvc *h = memalign(64, (sizeof(*h) + 63) & ~63u);
    if (!h)
        return NULL;
    memset(h, 0, sizeof(*h));
    if (!s_ddr)
        s_ddr = memalign(DDR_ALIGN, DDR_SIZE);
    L("hwo-ddr", (unsigned)(uintptr_t)s_ddr, 0, 0, 0);
    h->ps = memalign(64, (size_t)(sps_len + pps_len + 63) & ~63u);
    h->au = memalign(64, 64);
    if (!s_ddr || !h->ps || !h->au) {
        L("hwo-memory", 0, 0, 0, 0);
        hwavc_close(h);
        return NULL;
    }
    memcpy(h->ps, sps, (size_t)sps_len);
    memcpy(h->ps + sps_len, pps, (size_t)pps_len);
    h->sps_len = sps_len;
    h->pps_len = pps_len;
    h->nal_prefix = nal_prefix;
    if (session_open(h) != 0) {
        hwavc_close(h);
        return NULL;
    }
    return h;
}

/* A fresh library state for a new place in the video (a play, a jump): the decoder keeps pictures and state of its own that a key frame does not clear (PMPlayer Ultraviolet: "A discontinuity must
 * start from a fresh context even when the target sample is a valid IDR": it closes the decoder and opens it again at every seek). 0 when it is open again, else -1 (nothing is left open). */
int hwavc_reset(HwAvc *h)
{
    session_close(h);
    int r = session_open(h);
    if (r != 0) {
        session_close(h);
        return -1;
    }
    h->calls = 0; /* (the first samples of the new place are written to the log too) */
    return 0;
}

void hwavc_close(HwAvc *h)
{
    if (!h)
        return;
    session_close(h);
    free(h->ps);
    free(h->au);
    free(h);
}

int hwavc_decode(HwAvc *h, void *sample, int size, int mode, void *rgb[HWAVC_PICS], int *pics, int *w, int *h_out, unsigned *err)
{
    int log = h->calls < 3;
    h->calls++;
    *pics = 0;
    *err = 0;
    NalArg nal;
    nal.sps = h->ps;
    nal.sps_size = h->sps_len;
    nal.pps = h->ps + h->sps_len;
    nal.pps_size = h->pps_len;
    nal.nal_prefix = h->nal_prefix;
    nal.nal = sample;
    nal.nal_size = size;
    nal.mode = mode;
    sceKernelDcacheWritebackRange(sample, (unsigned)((size + 63) & ~63));
    int r = sceMpegGetAvcNalAu(&h->mpeg, &nal, h->au);
    if (log)
        L("hwd-nalau", (unsigned)r, (unsigned)size, (unsigned)mode, 0);
    if (r != 0) {
        *err = (unsigned)r;
        return -1;
    }
    SceInt32 n = 0;
    r = sceMpegAvcDecode(&h->mpeg, h->au, FRAME_W, NULL, &n);
    if (log)
        L("hwd-decode", (unsigned)r, (unsigned)n, 0, 0);
    if (r != 0) {
        *err = (unsigned)r;
        return -2;
    }
    Detail2 *d = NULL;
    r = sceMpegAvcDecodeDetail2(&h->mpeg, &d);
    if (log)
        L("hwd-detail", (unsigned)r, (unsigned)(uintptr_t)d, d ? (unsigned)(uintptr_t)d->info : 0, d ? (unsigned)(uintptr_t)d->yuv : 0);
    if (r != 0 || (n > 0 && (!d || !d->info || !d->yuv))) {
        *err = (unsigned)r;
        return -3;
    }
    if (n <= 0)
        return 0;
    if (n > HWAVC_MAX_OUT) { /* (a decoder that gives this many pictures at once is not working: the log of a PSP-1000 had three, at a key frame, where it puts out the three it held) */
        *err = 0x80000002u;
        L("hwd-many", (unsigned)n, 0, 0, 0);
        return -3;
    }
    if (n >= 3) /* (three or more at once: what a key frame does to the pictures the decoder held) */
        L("hwd-burst", (unsigned)n, (unsigned)mode, (unsigned)h->calls, 0);
    /* the pictures of this call are the elements of the arrays that the library points to, in the order they are to be shown */
    for (int k = 0; k < n && k < HWAVC_PICS; k++) { /* (the pictures after the first three are counted, not converted) */
        PicInfo *info = d->info + k;
        PicYuv *yuv = d->yuv + k;
        if (info->width <= 0 || info->width > FRAME_W || info->height <= 0 || info->height > 512) {
            *err = 0x80000001u;
            L("hwd-size", (unsigned)info->width, (unsigned)info->height, (unsigned)k, 0);
            return -3;
        }
        if (k == 0) {
            *w = info->width;
            *h_out = info->height;
        }
        if (!rgb || !rgb[k]) /* the picture is not wanted (the run up to a place a jump asked for): it is counted, not converted */
            continue;
        Csc csc;
        csc.height = (info->height + 15) >> 4;
        csc.width = (info->width + 15) >> 4;
        csc.mode0 = 0;
        csc.mode1 = 0;
        for (int i = 0; i < 8; i++)
            csc.buf[i] = yuv->buf[i];
        unsigned rgb_bytes = (unsigned)FRAME_W * (unsigned)((info->height + 15) & ~15) * 4;
        sceKernelDcacheWritebackInvalidateRange(rgb[k], rgb_bytes); /* (no old line of the picture's memory may be written over what the converter makes) */
        r = sceMpegBaseCscAvc(rgb[k], 0, FRAME_W, &csc);
        sceKernelDcacheWritebackInvalidateRange(rgb[k], rgb_bytes);
        if (log)
            L("hwd-csc", (unsigned)r, (unsigned)info->width, (unsigned)info->height, (unsigned)(n * 10 + k));
        if (r != 0) {
            *err = (unsigned)r;
            return -4;
        }
    }
    *pics = n;
    return 0;
}

#endif /* PSP5_HW_FAKE */
