/* psp5 authentic – the video engine (see video.h) */
#include <pspkernel.h>
#include <psputility.h>
#include <pspaudio.h>
#include <pspmpeg.h>
#include <pspgu.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "video.h"
#include "mp4.h"
#include "mpegps.h"
#include "aac.h"
#include "tscale.h"
#include "h264dec.h"
#include "unit.h"
#include "meboot.h"
#include "hwavc.h"
#include "hwpipe.h"

/* A PPSSPP test build (PSP5_TEST) lets PPSSPP's own decoder take every stream (its pictures are what the tests and the README show). With -DPSP5_VIDEO_REAL the test build decides as a PSP does
 * (which stream goes to the software decoder, which is refused), so that code runs in the tests too. */
#if defined(PSP5_TEST) && !defined(PSP5_VIDEO_REAL)
#define PSP5_ANY_STREAM 1
#endif
#include "music.h"
#include "system.h"
#include "logs.h"

#ifdef VIDEO_DEBUG
#include <stdarg.h>
void vlog(const char *fmt, ...)
{
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    SceUID fd = sceIoOpen("ms0:/PSP5/video.log", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, line, n > 0 ? (n < (int)sizeof(line) ? n : (int)sizeof(line) - 1) : 0);
        sceIoClose(fd);
    }
}
#define VLOG(...) vlog(__VA_ARGS__)
#else
#define VLOG(...) ((void)0)
#endif

/* What the video code says about its way (a "crumb": the name of a step and up to four numbers) is kept in memory (logs.c) and goes into PSP5/logs/Video.log only when something fails, together with a
 * sentence that says what (video_close writes it). A step that can stop a PSP (the Media Engine, the games' library, the closing) is marked on the card before it begins (logs_step), and the mark is
 * taken away when the video has been closed the right way: when the PSP stops in it, the next start of psp5 turns the mark into an entry of the log. No file names or other personal things are written. */
static void crumb_path(char *out, size_t n, const char *name) { snprintf(out, n, "%s/%s", PSP5_DATA_DIR, name); } /* (video-skip.txt: what the closing leaves out) */

static void crumb(const char *what, int a, int b) { logs_note(LOG_VIDEO, "%s %d %d", what, a, b); }
static void crumb4(const char *what, int a, int b, int c, int d) { logs_note(LOG_VIDEO, "%s %d %d %d %d", what, a, b, c, d); }
static void crumbh(const char *what, unsigned a, unsigned b, unsigned c, unsigned d) { logs_note(LOG_VIDEO, "%s %08x %08x %08x %08x", what, a, b, c, d); }

/* the first 16 bytes at an address, as four words (only an address in the user's memory is read: it comes from the PSP's library and nothing says it is one) */
static void crumb_mem(const char *what, unsigned addr)
{
    unsigned q = addr & 0x1FFFFFFF;
    if (q < 0x08800000u || q + 16 > 0x0C000000u || (addr & 3)) {
        crumbh(what, addr, 0, 0, 0);
        return;
    }
    const unsigned char *b = (const unsigned char *)(uintptr_t)addr;
    crumbh(what, (unsigned)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3], (unsigned)b[4] << 24 | b[5] << 16 | b[6] << 8 | b[7], (unsigned)b[8] << 24 | b[9] << 16 | b[10] << 8 | b[11],
           (unsigned)b[12] << 24 | b[13] << 16 | b[14] << 8 | b[15]);
}

/* The steps of the closing that crashed a PSP are never made again on it: when the mark that a video left behind (logs.h: the PSP stopped in it) names one of them, that step is written into video-skip.txt, and
 * the closing leaves it out from then on (the memory it would have freed is left to the PSP until psp5 ends). */
enum { TD_STOP, TD_FLUSH, TD_FREE_ES, TD_UNREG, TD_DELETE, TD_RING, TD_FINISH, TD_N };
static const char *const TD_NAME[TD_N] = { "td-stop", "td-flush", "td-free-es", "td-unregist", "td-delete", "td-ring", "td-finish" };
static unsigned s_skip;
static int s_poisoned; /* see video_poisoned() */

static void skip_load(void)
{
    char path[96], buf[256];
    crumb_path(path, sizeof(path), "video-skip.txt");
    s_skip = 0;
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return;
    int n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (n <= 0)
        return;
    buf[n] = 0;
    for (int i = 0; i < TD_N; i++)
        if (strstr(buf, TD_NAME[i]))
            s_skip |= 1u << i;
}

static void skip_save(void)
{
    char path[96], buf[256];
    int n = 0;
    crumb_path(path, sizeof(path), "video-skip.txt");
    for (int i = 0; i < TD_N; i++)
        if (s_skip & (1u << i))
            n += snprintf(buf + n, sizeof(buf) - (size_t)n, "%s\n", TD_NAME[i]);
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, buf, n);
        sceIoClose(fd);
    }
}

static void crumb_start(void)
{
    skip_load();
    const char *stopped = logs_stopped(LOG_VIDEO); /* the step an earlier run of psp5 was in when the PSP stopped: one of the closing's is never made again (see TD_NAME) */
    for (int i = 0; i < TD_N; i++)
        if (!strcmp(stopped, TD_NAME[i])) {
            s_skip |= 1u << i;
            skip_save();
        }
    logs_forget_stop(LOG_VIDEO);
    logs_step(LOG_VIDEO, "opening the video");
}

/* the video was closed the right way and played: nothing to leave behind */
static void crumb_end(void) { logs_done(LOG_VIDEO); }

/* A video that did not play (it could not be opened, the decoder gave errors, it was given up) is closed the right way too: its entry goes into Video.log, with the sentence that says what and the steps
 * before it. */
static void crumb_keep(const char *why)
{
    logs_fail(LOG_VIDEO, "%s", why);
    logs_done(LOG_VIDEO);
}

/* The PSP's own AAC decoder (sceAudiocodec, codec 0x1003) crashed a real PSP on its first frame (the log of an early test ended with "first-sound", written just before that call), and what
 * its parameter block wants cannot be found out without crashing more of them: the sound of an MP4 is made by a decoder of psp5's own (aac.c, AAC-LC in software, checked against ffmpeg). */

#define PKT 2048
#define RING_PACKETS 0x100            /* 512 KB of program stream in the ring buffer */
#define FRAME_W 512                   /* the width of the buffer the pictures are decoded into (a video is at most 512 wide) */
#define NO_DATA 0x80618001u           /* sceMpegGetAvcAu: nothing to give yet */
#define PTS0 90000                    /* the first time stamp of a PSMF (what the PSP's library expects of it) */
#define PCM_FRAMES 32768              /* the queue of sound: 0.74 seconds at 44.1 kHz (a power of two) */
#define BLOCK 1024                    /* sound frames to the sound output at a time */
#define MAX_AU (320 * 1024)           /* the biggest video sample that is read (the PSP's own limit for a picture is far below that) */

enum { VK_PMF, VK_MP4 };

#define RING(v) ((SceMpegRingbuffer *)(v)->ring_mem)

struct Video {
    int kind;
    int has_session_options;
    VideoOpenOptions session_options;
    int w, h;
    float duration;
    int has_sound;
    /* a PMF */
    FILE *fp;
    unsigned stream_off, stream_size;
    unsigned char header[2048];
    struct { unsigned pts, off; } *ep; /* the entry points: time stamp and the sector they are at */
    int n_ep;
    unsigned first_ts;
    int pmf_atrac;
    /* an MP4 */
    Mp4 *mp4;
    PsWriter ps;
    unsigned char *sample, *au[2]; /* the units the program stream writer holds (the one it writes and the one after it) */
    int au_cap, au_sel;
    UnitMaker um; /* makes the units of the MP4 (parameter sets, the PSP's timing) */
    int opt_soft; /* test switch (video-opts.txt): 1 = soft=1 (the software decoder for every stream), 0 = soft=0 (never), -1 = as the stream needs */
    int soft_why; /* H264 decoder refusal reason from h264d_new */
    int opt_hw;   /* test switch: 1 = hw=1 (a CAVLC stream without B pictures is tried on the PSP's own decoder, see hwavc.h; the software decoder takes it when that does not work), 3 = hw=3 (the same, the SPS's profile left as it is) */
    HwAvc *hwd;   /* the PSP's own decoder, when this stream is decoded by it: the pictures are RGBA (GU_PSM_8888) in fbuf, made by the worker thread like the ones of the software decoder */
    unsigned char *hwbuf; /* a sample as that decoder takes it (aligned to 64 bytes) */
    int hw_boot;          /* the Engine's image the PSP's own decoder runs with (MEBOOT_BASELINE, MEBOOT_MAIN), 0 when it does not */
    int hw_tried;         /* the PSP's own decoder was tried (the function that starts the Engine's image was found): when it did not play to the end, the log gets an entry */
    int hw_first;         /* the next sample is given to it as a first one (mode 3, after a jump) */
    volatile int hw_err;  /* it did not take a sample: the screen's thread goes back to the software decoder */
    const char *why;      /* what stopped the video, in words (a static text), for the entry of the log */
    volatile int read_err; /* the card did not give a sample (a file that was open before the PSP slept does not read after it): the video ends, no other way is tried: it reads the same file */
    HwPipe hwp;           /* what goes to the PSP's decoder and when a picture is shown (hwpipe.c) */
    int hw_from;          /* the first sample of this play */
    int hw_pub;           /* pictures that went to the screen since the play started (the log of the end says how many of the video's pictures there were) */
    double hw_late;       /* how late the last picture was when it was shown (for the speed control) */
    int hw_flushing;
    int soft;     /* the pictures are made by h264dec.c: the stream is one the PSP's decoder cannot take (CAVLC, a High profile) */
    H264Dec *dec;
    int soft_up, pending;
    double pend_ts;
    unsigned soft_us, soft_late_n, soft_skip_n;
    unsigned long long last_pub;
    /* the thread that makes the pictures of a stream in software: it reads the file, decodes the sound and the pictures and makes the RGB; the screen loop only takes what is ready */
    SceUID wthread;
    volatile int wquit;
    void *fbuf[4];                 /* three RGB pictures: the one on the screen (front), the one that is ready (rdy, -1 when none) and the one that is being made; the PSP's decoder has a fourth (B pictures: two that wait for their time) */
    volatile int front, rdy;
    volatile int clock_ms;         /* the clock, for the thread */
    int aud_errors;
    volatile unsigned gap_max, gap_late, blocks; /* the sound thread: the longest time between two blocks of sound given to the output (a block is 23 ms), how many were later than 34 ms, how many were given */
    unsigned char *dump;           /* test switch pcm=1: the first seconds of the sound as it was given to the output, written to PSP5/video-pcm.raw when the video is closed */
    unsigned dump_n, dump_cap;
    int opt_pcm;
    unsigned long long t_play0;
    volatile float speed;          /* how fast a video in software plays: 1 = as it is; below that, the sound is played slower (resampled) and the clock goes slower with it, when the decoder cannot keep up */
    float speed_min;
    unsigned long long ctl_t, lf_block;
    int opt_slow, opt_burn;
    volatile unsigned t_io, t_conv, t_aud, underruns; /* microseconds spent reading, converting and decoding sound; times the sound ran dry (for the log) */
    int quiet;    /* closed without a trace (refused before anything was done) */
    int ge;       /* a picture made in software is given as planes and drawn by the graphics chip (video_yuv); 0: made into RGB by the CPU (test switch ge=0) */
    int opt_ge;
    VideoYuv yuv[3];
    void *ybuf[3];
    int vnext, anext;
    int src_eof;
    /* sceMpeg */
    int mpeg_up, lib_up;
    SceMpeg mpeg;
    unsigned char ring_mem[96] __attribute__((aligned(16))); /* the ring buffer's structure: the library writes more into it than the SDK's declaration says (the PSP's own samples use 52 bytes), so it has room */
    void *ring_data, *mpeg_mem;
    int ring_size, mpeg_size;
    SceMpegStream *s_avc, *s_atrac;
    void *es_avc, *es_atrac;
    SceMpegAu au_avc, au_atrac;
    int atrac_out;
    unsigned char *pcm_tmp;
    int have_au, atrac_first;
    double au_ts, last_ts, frame_dur; /* the time of the unit that was given (its decoding time, or what follows the one before when it has none) */
    int have_ts;
    void *frame;
    VideoFrame pic;
    int have_pic;
    /* the sound */
    int audio_ok;
    int rate;
    SceUID athread;
    volatile int quit, paused, reset, started;
    short *pcm;
    volatile int head, tail; /* frames, counted up and masked */
    volatile unsigned played; /* frames the sound output has taken since the sound started */
    int src_reserved;
    AacDec *aac;
    unsigned char *aac_in;
    short *aac_out;
    double a_base;
    int a_has_base;
    /* time */
    double clock, skip_until;
    int clock_audio;
    float idle;
    int ended;
    volatile int failed; /* written by the software decoder thread, observed by the player thread */
    int no_data, dec_errors, audio_dry;
    int stream_error;
    int decoded;                      /* the picture decoder has been asked for a picture in this session (it is only stopped then) */
    int n_put, n_au, n_dec;           /* packets fed, units given, pictures asked for (for the log) */
    int err_total;                    /* errors of the library in all (feeding, units, decoding): the log gets an entry when there were any */
    unsigned seen[6];                 /* the first different answers of sceMpegGetAvcAu (for the log) */
    int n_seen, hb_lines;
    float hb, since;                  /* the time to the next sign of life in the log's steps, and the time this session has gone without a picture */
    int stage; /* what this session has done already, for the log (bits): 1 = fed, 2 = first picture asked for, 4 = first sound asked for, 8 = first put answered, 16 = first unit seen */
};

static void video_mark_failed(Video *v)
{
    __sync_lock_test_and_set(&v->failed, 1);
}

/* the PSP's decoders are loaded once, the first time a video is opened */
static int s_modules;

static int load_modules(void)
{
    if (s_modules)
        return s_modules > 0;
    int ok = 1;
    int r = sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
    if (r < 0 && (unsigned)r != 0x80111102u)
        ok = 0;
    r = sceUtilityLoadModule(PSP_MODULE_AV_MPEGBASE);
    if (r < 0 && (unsigned)r != 0x80111102u)
        ok = 0;
    sceUtilityLoadModule(PSP_MODULE_AV_ATRAC3PLUS); /* (the sound of a PMF; without it a PMF plays without sound) */
    s_modules = ok ? 1 : -1;
    return ok;
}

static unsigned be32(const unsigned char *p) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }
static unsigned long long be48(const unsigned char *p) { return ((unsigned long long)be32(p) << 16) | ((unsigned)p[4] << 8) | p[5]; }
static unsigned long long au_pts(const SceMpegAu *a) { return ((unsigned long long)a->iPtsMSB << 32) | a->iPts; }
static double au_sec(const SceMpegAu *a) { return ((double)au_pts(a) - PTS0) / 90000.0; }
/* a time stamp the PSP's library gives for a unit that has one: -1 (all ones) is none */
static int ts_valid(unsigned msb, unsigned lo) { return msb == 0 && lo != 0xFFFFFFFFu; }
static int pow2(int n) { int p = 1; while (p < n) p <<= 1; return p; }
static unsigned long long now_us(void) { return sceKernelGetSystemTimeWide(); }
/* the memory of the planes of one picture for the graphics chip: Y rows of 512, U and V rows of 256, each plane a multiple of 64 bytes */
static size_t ysz_of(int h) { return ((size_t)512 * (size_t)h + 63) & ~(size_t)63; }
static size_t csz_of(int h) { return ((size_t)256 * (size_t)((h + 1) / 2) + 63) & ~(size_t)63; }
static int soft_start(Video *v);
static void soft_stop(Video *v);
static float soft_pick_start(Video *v, float at);

/* ---------------------------------------------------------------------- */
/* what the ring buffer is fed with */

/* video sample i of the MP4 made into what the program stream carries, in buf: its length, or -1 when the sample is left out (it cannot be read or understood); *key is 1 when the unit has an
 * SPS (it starts a group) */
static int mp4_convert(Video *v, int i, int *key, unsigned char *buf)
{
    return unit_make(&v->um, i, buf, v->au_cap + 4096, key);
}

/* the size of unit j for the planning of a group (it is made into a buffer that nothing holds then) */
static int mp4_size(void *arg, int j, int *key)
{
    Video *v = arg;
    return mp4_convert(v, j, key, v->au[0]);
}

/* the next unit for the program stream writer: it takes turns with two buffers */
static int mp4_pull(void *arg, const uint8_t **data, int *len, int64_t *pts, int64_t *dts)
{
    Video *v = arg;
    while (v->vnext < mp4_video_count(v->mp4)) {
        int i = v->vnext++, key;
        unsigned char *buf = v->au[v->au_sel];
        int l = mp4_convert(v, i, &key, buf);
        if (l < 0)
            continue;
        float d, p;
        mp4_video_times(v->mp4, i, &d, &p, &key);
        *data = buf;
        *len = l;
        *pts = (int64_t)((double)p * 90000.0) + PTS0;
        *dts = (int64_t)((double)d * 90000.0) + PTS0;
        v->au_sel ^= 1;
        return 1;
    }
    return 0;
}

static SceInt32 ring_cb(ScePVoid data, SceInt32 packets, ScePVoid param)
{
    Video *v = param;
    if (v->kind == VK_PMF) {
        size_t got = fread(data, 1, (size_t)packets * PKT, v->fp);
        sceKernelDcacheWritebackInvalidateRange(data, (unsigned)((got / PKT) * PKT));
        return (SceInt32)(got / PKT);
    }
    int n = 0;
    while (n < packets && ps_sector(&v->ps, (unsigned char *)data + (size_t)n * PKT))
        n++;
    /* the sectors were written by the CPU, into its cache: the decoder of the PSP (which is another processor) reads the memory, so they are written out to it */
    sceKernelDcacheWritebackInvalidateRange(data, (unsigned)n * PKT);
    return n;
}

/* ---------------------------------------------------------------------- */
/* the sound */

static int rate_ok(int rate)
{
    static const int R[] = { 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000 };
    for (unsigned i = 0; i < sizeof(R) / sizeof(R[0]); i++)
        if (R[i] == rate)
            return 1;
    return 0;
}

static int pcm_count(const Video *v) { return (v->head - v->tail) & 0x7FFFFFFF; }

/* Release resources acquired before an audio thread successfully starts. No thread wait is valid here. */
static void audio_release_unstarted(Video *v)
{
    if (v->src_reserved) {
        for (int i = 0; i < 100 && sceAudioSRCChRelease() < 0; i++)
            sceKernelDelayThread(2000);
        v->src_reserved = 0;
    }
    free(v->pcm);
    v->pcm = NULL;
}

static void pcm_push(Video *v, const short *frames, int n)
{
    for (int i = 0; i < n; i++) {
        int at = (v->head + i) & (PCM_FRAMES - 1);
        v->pcm[at * 2] = frames[i * 2];
        v->pcm[at * 2 + 1] = frames[i * 2 + 1];
    }
    __sync_synchronize();
    v->head += n;
}

static int audio_thread(SceSize args, void *argp)
{
    Video *v = *(Video **)argp;
    short *block = memalign(64, BLOCK * 8);
    short *win = memalign(64, TS_WINDOW * 4);
    TScale *ts = memalign(64, sizeof(TScale));
    int starving = 0, flip = 0, stretch = 0, calm = 0;
    unsigned long long tprev = 0;
    while (!v->quit && block && win && ts) {
        if (v->reset) {
            v->tail = v->head;
            v->played = 0;
            stretch = 0;
            __sync_synchronize();
            v->reset = 0;
            continue;
        }
        float sp = v->speed;
        /* The video is slowed down (the PSP cannot decode it as fast as it plays): the sound is made longer with it, with its pitch as it was (tscale.c). It starts when the speed goes below one and
         * stops three seconds after it is back at one (the sound then goes on where the last piece would have gone on: no jump). */
        if (stretch && sp >= 0.9995f && ++calm > 130) {
            v->tail = (int)(ts->prev_start + TS_HOP);
            v->played = (unsigned)v->tail;
            stretch = 0;
        } else if (stretch && sp < 0.9995f) {
            calm = 0;
        }
        if (!stretch && sp < 0.9995f && v->tail > 200) {
            ts_begin(ts, (long long)v->tail);
            stretch = 1;
            calm = 0;
        }
        int ready;
        if (stretch)
            ready = (long long)v->head >= ts_low(ts) + TS_WINDOW + TS_HOP;
        else
            ready = pcm_count(v) >= BLOCK;
        if (v->paused || !v->started || !ready) {
            tprev = 0; /* (a wait is not a gap between two blocks) */
            if (v->started && !v->paused && (v->src_eof & 2) && pcm_count(v) > 0)
                v->tail = v->head; /* (the sound is at its end and what is left is less than a block, or less than the window the stretching needs: it is not played; the clock then goes on by itself, as it does when the sound has all been played) */
            if (v->started && !v->paused && !(v->src_eof & 2) && !starving) {
                starving = 1;
                v->underruns++; /* the sound ran dry (not at its end) */
            }
            sceKernelDelayThread(4000);
            continue;
        }
        starving = 0;
        short *blk = block + (flip ? BLOCK * 2 : 0); /* two blocks in turn: the one just given to the output is not written over */
        flip ^= 1;
        if (stretch) {
            for (int h = 0; h < BLOCK / TS_HOP; h++) {
                long long low = ts_low(ts);
                for (int i = 0; i < TS_WINDOW; i++) {
                    int at = (int)((low + i) & (PCM_FRAMES - 1));
                    win[i * 2] = v->pcm[at * 2];
                    win[i * 2 + 1] = v->pcm[at * 2 + 1];
                }
                float s2 = v->speed;
                s2 = s2 > 1.0f ? 1.0f : s2 < 0.5f ? 0.5f : s2;
                ts_hop(ts, win, (double)TS_HOP * (double)s2, blk + h * TS_HOP * 2);
            }
            long long nlow = ts_low(ts);
            if (nlow > v->tail)
                v->tail = (int)nlow;
            v->played = (unsigned)(long long)ts->nom; /* where in the sound this block is, in frames */
        } else {
            for (int i = 0; i < BLOCK; i++) {
                int at = (v->tail + i) & (PCM_FRAMES - 1);
                blk[i * 2] = v->pcm[at * 2];
                blk[i * 2 + 1] = v->pcm[at * 2 + 1];
            }
            v->tail += BLOCK;
            v->played += BLOCK;
        }
        if (v->dump && v->dump_n + BLOCK * 4 <= v->dump_cap) {
            memcpy(v->dump + v->dump_n, blk, BLOCK * 4);
            v->dump_n += BLOCK * 4;
        }
        sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, blk);
        unsigned long long tn = now_us();
        if (tprev) {
            unsigned gap = (unsigned)(tn - tprev);
            if (gap > v->gap_max)
                v->gap_max = gap;
            if (gap > 34000)
                v->gap_late++;
        }
        tprev = tn;
        v->blocks++;
    }
    if (!v->quit && (!block || !win || !ts)) {
        v->why = "not enough memory for the video audio thread";
        __sync_synchronize();
        video_mark_failed(v);
    }
    free(block);
    free(win);
    free(ts);
    return 0;
}

static int audio_start(Video *v, int rate)
{
    if (!rate_ok(rate))
        return 0;
    for (int i = 0; i < 100 && music_state() != MUSIC_STOPPED; i++)
        sceKernelDelayThread(10000); /* the music thread gives its sound channel back when it has stopped */
    int r = -1;
    for (int i = 0; i < 40; i++) {
        r = sceAudioSRCChReserve(BLOCK, rate, 2);
        if (r >= 0)
            break;
        sceKernelDelayThread(25000);
    }
    if (r < 0)
        return 0;
    v->src_reserved = 1;
    v->rate = rate;
    v->pcm = memalign(64, (size_t)PCM_FRAMES * 4);
    if (!v->pcm) {
        audio_release_unstarted(v);
        return 0;
    }
    SceUID thread = sceKernelCreateThread("psp5_vsound", audio_thread, 0x14, 0x4000, 0, NULL); /* (as high as the music player's: it only has to copy a block every 23 ms, and a late block is a crackle) */
    if (thread < 0) {
        audio_release_unstarted(v);
        return -1;
    }
    Video *self = v;
    int start = sceKernelStartThread(thread, sizeof(self), &self);
    if (start < 0) {
        sceKernelDeleteThread(thread); /* it never ran: do not wait for it */
        audio_release_unstarted(v);
        return -1;
    }
    v->athread = thread;
    return 1;
}

static void audio_stop(Video *v)
{
    v->quit = 1;
    if (v->athread > 0) {
        sceKernelWaitThreadEnd(v->athread, NULL);
        sceKernelDeleteThread(v->athread);
        v->athread = 0;
    }
    if (v->src_reserved) {
        for (int i = 0; i < 100 && sceAudioSRCChRelease() < 0; i++)
            sceKernelDelayThread(2000);
        v->src_reserved = 0;
    }
    free(v->pcm);
    v->pcm = NULL;
}

/* the AAC decoder for an MP4 (aac.c): 0 when the sound is not one it can do (the video then plays without sound) */
static int aac_open(Video *v)
{
    int cfg_len, rate = 0, chans = 0;
    const unsigned char *cfg = mp4_audio_config(v->mp4, &cfg_len);
    v->aac = aac_new(cfg, cfg_len, &rate, &chans);
    v->aac_in = malloc(8192);
    v->aac_out = malloc((size_t)AAC_FRAME * 4);
    if (!v->aac || !v->aac_in || !v->aac_out || rate != mp4_audio_rate(v->mp4))
        return 0;
    return 1;
}

static void aac_close(Video *v)
{
    aac_free(v->aac);
    free(v->aac_in);
    free(v->aac_out);
    v->aac = NULL;
    v->aac_in = NULL;
    v->aac_out = NULL;
}

/* one AAC frame of the MP4: 1 when it was taken from the file (it gave sound, or it could not be read and the sound goes on with the next one) */
static int decode_aac(Video *v)
{
    if (v->anext >= mp4_audio_count(v->mp4)) {
        v->src_eof |= 2; /* (the sound is at its end) */
        return 0;
    }
    int i = v->anext++;
    unsigned long long t0 = now_us();
    int size = mp4_audio_size(v->mp4, i);
    if (size <= 0 || size > 8192 || mp4_audio_read(v->mp4, i, v->aac_in, 8192) != size)
        return 1; /* a frame that cannot be read is skipped: the sound goes on with the next */
    if (!v->a_has_base) {
        v->a_base = mp4_audio_time(v->mp4, i);
        v->a_has_base = 1;
    }
    if (!(v->stage & 4)) {
        v->stage |= 4;
        crumb("first-sound", size, 0);
    }
    if (!aac_decode(v->aac, v->aac_in, size, v->aac_out)) {
        if (++v->aud_errors > 400)
            v->audio_ok = 0; /* it does not work: the video goes on without sound */
    }
    pcm_push(v, v->aac_out, AAC_FRAME);
    v->t_aud += (unsigned)(now_us() - t0);
    return 1;
}

/* one audio unit of a PMF */
static int decode_atrac(Video *v)
{
    SceInt32 unk = 0;
    int r = sceMpegGetAtracAu(&v->mpeg, v->s_atrac, &v->au_atrac, &unk);
    if (r != 0) {
        if ((v->src_eof & 1) && ++v->audio_dry > 30)
            v->src_eof |= 2; /* the stream is read to its end and no more sound comes out of it */
        return 0; /* nothing yet (NO_DATA) */
    }
    v->audio_dry = 0;
    if (!v->a_has_base && au_sec(&v->au_atrac) < v->skip_until - 0.03)
        return 2; /* a jump: the sound before the time that was asked for is taken out of the stream and not played */
    if (!v->a_has_base) {
        v->a_base = au_sec(&v->au_atrac);
        v->a_has_base = 1;
    }
    r = sceMpegAtracDecode(&v->mpeg, &v->au_atrac, v->pcm_tmp, v->atrac_first);
    v->atrac_first = 0;
    if (r != 0) {
        if (++v->dec_errors > 40)
            v->audio_ok = 0;
        return 1;
    }
    sceKernelDcacheWritebackInvalidateRange(v->pcm_tmp, v->atrac_out);
    pcm_push(v, (const short *)v->pcm_tmp, v->atrac_out / 4);
    return 1;
}

/* keeps the queue of sound at about half a second */
static void pump_audio(Video *v)
{
    if (!v->audio_ok || !v->pcm)
        return;
    for (int guard = 0, skipped = 0; guard < 8 && skipped < 600 && pcm_count(v) < v->rate / 2 && pcm_count(v) + 2048 < PCM_FRAMES; guard++) {
        int got = v->kind == VK_MP4 ? decode_aac(v) : decode_atrac(v);
        if (!got)
            break;
        if (got == 2) { /* skipped, not decoded: does not count as one of the units of this round */
            guard--;
            skipped++;
            continue;
        }
        if (!v->started && pcm_count(v) >= v->rate / 4) { /* enough to start with */
            v->started = 1;
            v->clock_audio = 1;
            VLOG("sound starts: %d frames, at %.3f s\n", pcm_count(v), v->a_base);
        }
    }
    if (!v->started && (v->src_eof & 2) && pcm_count(v) > 0)
        v->started = 1, v->clock_audio = 1;
}

/* ---------------------------------------------------------------------- */
/* the session of sceMpeg */

static void make_header(Video *v, unsigned char *h)
{
    /* what the PSP's library reads of the head of a PSMF: "PSMF0014", where the stream starts (the second sector) and how long it is, the first and last time stamp, one video stream with its size in
     * blocks of 16. Copied from a PMF of the PSP's own, with the numbers of this video put in. */
    static const unsigned char T[0x92] = {
        0x50, 0x53, 0x4d, 0x46, 0x30, 0x30, 0x31, 0x34, 0x00, 0x00, 0x08, 0x00, 0x00, 0x04, 0x08, 0x00, [0x50] = 0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x00, 0x01, 0x5f, 0x90, 0x00, 0x00, 0x00, 0x0a, 0x14, 0x5a,
        0x00, 0x00, 0x61, 0xa8, 0x00, 0x01, 0x5f, 0x90, 0x01, 0x01, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x01, 0x5f, 0x90, 0x00, 0x00, 0x00, 0x0a, 0x14, 0x5a, 0x00, 0x01, 0x00, 0x00, 0x00, 0x12,
        0x00, 0x01, 0xe0, 0x00, 0x20, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x05
    };
    memset(h, 0, 2048);
    memcpy(h, T, sizeof(T));
    unsigned long long last = (unsigned long long)(v->duration * 90000.0) + PTS0;
    unsigned size = (unsigned)mp4_video_count(v->mp4) * PKT; /* (about: the real length is only known when the end is reached) */
    h[12] = (unsigned char)(size >> 24); h[13] = (unsigned char)(size >> 16); h[14] = (unsigned char)(size >> 8); h[15] = (unsigned char)size;
    for (int k = 0; k < 6; k++) {
        h[0x5A + k] = (unsigned char)(last >> (8 * (5 - k)));
        h[0x74 + k] = (unsigned char)(last >> (8 * (5 - k)));
    }
    h[0x8E] = (unsigned char)((v->w + 15) / 16);
    h[0x8F] = (unsigned char)((v->h + 15) / 16);
}

/* one step of the closing: its mark goes onto the card first (logs_step), and it is left out when it stopped this PSP before (see TD_NAME) */
#define TD_STEP(step) (logs_step(LOG_VIDEO, TD_NAME[step]), (s_skip & (1u << (step))) ? (crumb("td-skipped", step, 0), VLOG("closing: %s left out\n", TD_NAME[step]), 0) : 1)

static void session_destroy(Video *v)
{
    if (!v->mpeg_up)
        return;
    SceInt32 stop_status = 0;
    /* the end of the decoding and the flush belong to a decoder that has been used: a PSP died in them when it was closed before it had got a single picture */
    if (v->decoded) {
        if (TD_STEP(TD_STOP))
            sceMpegAvcDecodeStop(&v->mpeg, FRAME_W, &v->frame, &stop_status);
        if (TD_STEP(TD_FLUSH))
            sceMpegFlushAllStream(&v->mpeg);
    }
    v->decoded = 0;
    if (v->es_atrac)
        free(v->es_atrac);
    if (v->es_avc && TD_STEP(TD_FREE_ES))
        sceMpegFreeAvcEsBuf(&v->mpeg, v->es_avc);
    /* A PSP died in sceMpegUnRegistStream when it was given the handle itself, as the SDK's header says (the first test, with a clean session of the PSP's own PMF): like all the other calls it takes
     * the address of the handle. After decode errors the stream is left registered (sceMpegDelete ends it). */
    if (v->dec_errors > 0 || video_failed(v)) {
        crumb("td-unregist-skipped", v->dec_errors, video_failed(v));
    } else if (TD_STEP(TD_UNREG)) {
        if (v->s_avc)
            sceMpegUnRegistStream((SceMpeg)&v->mpeg, v->s_avc);
        if (v->s_atrac)
            sceMpegUnRegistStream((SceMpeg)&v->mpeg, v->s_atrac);
    }
    if (TD_STEP(TD_DELETE))
        sceMpegDelete(&v->mpeg);
    if (TD_STEP(TD_RING))
        sceMpegRingbufferDestruct(RING(v));
    crumb("td-done", 0, 0);
    free(v->pcm_tmp);
    v->es_atrac = v->es_avc = NULL;
    v->pcm_tmp = NULL;
    v->s_avc = v->s_atrac = NULL;
    v->mpeg_up = 0;
}

static int session_create(Video *v)
{
    if (v->has_session_options)
        return 0; /* session-policy videos must never enter the legacy MPEG route */
    if (!v->ring_data || !v->mpeg_mem)
        return 0;
    crumb("session", 0, 0);
    logs_step(LOG_VIDEO, "the games' library: a session opens");
    int rc = sceMpegRingbufferConstruct(RING(v), RING_PACKETS, v->ring_data, v->ring_size, ring_cb, v);
    VLOG("construct %08x\n", rc);
    if (rc < 0) {
        crumbh("fail-construct", (unsigned)rc, 0, 0, 0);
        return 0;
    }
    crumb("mpeg-create", v->mpeg_size, v->ring_size);
    int cr = sceMpegCreate(&v->mpeg, v->mpeg_mem, v->mpeg_size, RING(v), FRAME_W, 0, 0);
    VLOG("create %08x\n", cr);
    if (cr < 0) {
        crumbh("fail-create", (unsigned)cr, 0, 0, 0);
        sceMpegRingbufferDestruct(RING(v));
        return 0;
    }
    v->mpeg_up = 1;
    SceMpegAvcMode mode = { -1, SCE_MPEG_AVC_FORMAT_5650 };
    SceInt32 off = 0, size = 0;
    crumbh("decode-mode", (unsigned)sceMpegAvcDecodeMode(&v->mpeg, &mode), 0, 0, 0);
    if (v->kind == VK_MP4)
        make_header(v, v->header);
    int qo = sceMpegQueryStreamOffset(&v->mpeg, v->header, &off), qs = sceMpegQueryStreamSize(v->header, &size);
    VLOG("query offset %08x (%d) size %08x (%d)\n", qo, (int)off, qs, (int)size);
    if (qo < 0 || qs < 0) {
        crumbh("fail-query", (unsigned)qo, (unsigned)qs, (unsigned)off, (unsigned)size);
        goto fail_up;
    }
    v->s_avc = sceMpegRegistStream(&v->mpeg, 0, 0);
    VLOG("regist avc %p\n", (void *)v->s_avc);
    if (!v->s_avc) {
        crumb("fail-regist", 0, 0);
        goto fail_up;
    }
    if (v->kind == VK_PMF && v->pmf_atrac && v->audio_ok)
        v->s_atrac = sceMpegRegistStream(&v->mpeg, 1, 0);
    v->es_avc = sceMpegMallocAvcEsBuf(&v->mpeg);
    int ia = v->es_avc ? sceMpegInitAu(&v->mpeg, v->es_avc, &v->au_avc) : -1;
    VLOG("es %p init au %08x\n", v->es_avc, ia);
    crumbh("setup", (unsigned)(uintptr_t)v->s_avc, (unsigned)(uintptr_t)v->es_avc, (unsigned)ia, (unsigned)v->au_avc.iEsBuffer);
    if (ia < 0) {
        crumb("fail-initau", ia, 0);
        goto fail_up;
    }
    if (v->s_atrac) {
        SceInt32 es = 0, out = 0;
        if (sceMpegQueryAtracEsSize(&v->mpeg, &es, &out) < 0 || es <= 0 || out <= 0)
            goto fail_up;
        v->es_atrac = memalign(64, (size_t)es);
        v->pcm_tmp = memalign(64, (size_t)out);
        v->atrac_out = out;
        if (!v->es_atrac || !v->pcm_tmp || sceMpegInitAu(&v->mpeg, v->es_atrac, &v->au_atrac) < 0)
            goto fail_up;
    }
    return 1;
fail_up:
    session_destroy(v);
    return 0;
}

/* tops the ring buffer up from the source */
static void feed(Video *v)
{
    if (v->src_eof & 1)
        return;
    int free_packets = sceMpegRingbufferAvailableSize(RING(v));
    if (free_packets <= 0)
        return;
    int want = free_packets > 32 ? 32 : free_packets;
    if (!(v->stage & 1)) {
        v->stage |= 1;
        crumb("first-feed", want, free_packets);
        logs_step(LOG_VIDEO, "the games' library: the first sample goes in");
    }
    int r = sceMpegRingbufferPut(RING(v), want, free_packets);
    if (!(v->stage & 8)) {
        v->stage |= 8;
        crumb4("first-put", r, want, free_packets, v->src_eof);
    }
    if (r < 0) {
        VLOG("ring put %08x\n", r);
        if (v->stream_error < 3)
            crumb("put-error", r, v->stream_error);
        v->stream_error++;
        v->err_total++;
    } else if (r == 0) {
        v->src_eof |= 1;
        crumb("source-end", v->n_put, 0);
    } else {
        v->n_put += r;
    }
}

/* ---------------------------------------------------------------------- */
/* open, start, close */

static void pmf_header_info(Video *v)
{
    const unsigned char *h = v->header;
    unsigned long long first = be48(h + 0x54), last = be48(h + 0x5A);
    v->first_ts = (unsigned)first;
    v->duration = last > first ? (float)((double)(last - first) / 90000.0) : 0;
    int streams = (h[0x80] << 8) | h[0x81];
    for (int i = 0; i < streams && i < 8; i++) {
        const unsigned char *e = h + 0x82 + 16 * i;
        if ((e[0] & 0xF0) == 0xE0 && !v->w) {
            v->w = e[12] * 16;
            v->h = e[13] * 16;
            unsigned ep_off = be32(e + 4), n = be32(e + 8);
            if (n > 0 && n < 100000 && ep_off + n * 10 <= v->stream_off) {
                /* the entry points are in the part of the header that is read in full (stream_off bytes) */
                v->n_ep = (int)n;
            }
        } else if (e[0] == 0xBD && (e[1] & 0xF0) == 0) {
            v->pmf_atrac = 1;
        }
    }
}

int video_poisoned(void) { return s_poisoned; }

/* the software decoder for the stream of the MP4 (h264dec.c); 0 when it cannot take it */
static int soft_prepare(Video *v)
{
    unsigned char pp[1024];
    int pl = mp4_param_sets(v->mp4, pp, sizeof(pp)), why = 0;
    v->dec = pl > 0 ? h264d_new(pp, pl, mp4_nal_size(v->mp4), &why) : NULL;
    v->soft_why = why;
    if (!v->dec) {
        crumb4("stream", v->um.nref, v->um.wmb * 1000 + v->um.hmb, v->um.cabac * 100 + v->um.weighted * 10 + v->um.to_main, v->um.nref0);
        crumb4("soft-refused", why, v->um.unsupported, v->um.profile, v->um.cabac);
        v->why = "the software decoder cannot take this stream";
        return 0;
    }
    v->soft = 1;
    v->w = h264d_width(v->dec);
    v->h = h264d_height(v->dec);
    crumb4("soft", v->w, v->h, h264d_nref(v->dec), v->um.unsupported);
    return 1;
}

/* the three pictures for the thread of the software decoder (the first RGB one is the one the texture starts with): planes for the graphics chip, or RGB made by the CPU */
static int soft_buffers(Video *v, size_t frame_bytes)
{
    v->ge = v->hwd ? 0 : v->opt_ge; /* (the hardware decoder gives RGB: no planes) */
    int nb = v->hwd ? 4 : 3; /* (the PSP's decoder with B pictures: two pictures may wait for their time besides the one on the screen and the ready one) */
    for (int k = 0; k < nb; k++) {
        if (v->ge) {
            size_t ysz = ysz_of(v->h), csz = csz_of(v->h);
            if (!v->ybuf[k])
                v->ybuf[k] = memalign(64, ysz + 2 * csz);
            if (!v->ybuf[k])
                return 0;
            memset(v->ybuf[k], 128, ysz + 2 * csz);
            v->yuv[k].plane[0] = v->ybuf[k];
            v->yuv[k].plane[1] = (unsigned char *)v->ybuf[k] + ysz;
            v->yuv[k].plane[2] = (unsigned char *)v->ybuf[k] + ysz + csz;
            v->yuv[k].w = v->w;
            v->yuv[k].h = v->h;
            v->yuv[k].ystride = 512;
            v->yuv[k].cstride = 256;
            sceKernelDcacheWritebackInvalidateRange(v->ybuf[k], (unsigned)(ysz + 2 * csz));
        } else {
            if (!v->fbuf[k])
                v->fbuf[k] = k ? memalign(64, frame_bytes) : v->frame;
            if (!v->fbuf[k]) {
                crumbh("mem-fail", (unsigned)k, (unsigned)frame_bytes, (unsigned)sceKernelMaxFreeMemSize(), (unsigned)sceKernelTotalFreeMemSize()); /* the picture buffer that could not be made, its size, the biggest free block, the free memory */
                return 0;
            }
            memset(v->fbuf[k], 0, frame_bytes);
            sceKernelDcacheWritebackInvalidateRange(v->fbuf[k], (unsigned)frame_bytes);
        }
    }
    v->front = 0;
    v->rdy = -1;
    return 1;
}

/* ---------------------------------------------------------------------- */
/* the PSP's own decoder (hwavc.c, meboot.c) for a CAVLC stream; the software decoder is the way back */

static void hw_log(const char *tag, unsigned a, unsigned b, unsigned c, unsigned d) { crumbh(tag, a, b, c, d); }

/* The library of the games and the VSH's cannot be loaded at the same time: the one of the games (loaded for the first video that needs it) is given back before the VSH's is. 1 when it is not
 * loaded (any more). */
static int game_lib_unload(void)
{
    if (s_modules <= 0)
        return 1;
    int r = sceUtilityUnloadModule(PSP_MODULE_AV_MPEGBASE);
    crumbh("hw-unload-game", (unsigned)r, 0, 0, 0);
    if (r < 0)
        return 0;
    s_modules = 0; /* (the next video that needs the games' library loads it again) */
    return 1;
}

/* the SPS and the PPS of the MP4 as NAL units without start codes (the header byte first): their lengths, 0 when they are not there */
static int hw_param_sets(Video *v, unsigned char *sps, int *sl, unsigned char *pps, int *pl, int cap)
{
    unsigned char pp[1024];
    int n = mp4_param_sets(v->mp4, pp, sizeof(pp));
    *sl = *pl = 0;
    for (int i = 0; i + 3 < n;) {
        if (pp[i] == 0 && pp[i + 1] == 0 && pp[i + 2] == 1) {
            int st = i + 3, e = st;
            while (e + 2 < n && !(pp[e] == 0 && pp[e + 1] == 0 && pp[e + 2] <= 1))
                e++;
            if (e + 2 >= n)
                e = n;
            int type = pp[st] & 0x1F, len = e - st;
            if (len > 0 && len <= cap) {
                if (type == 7 && !*sl) {
                    memcpy(sps, pp + st, (size_t)len);
                    *sl = len;
                } else if (type == 8 && !*pl) {
                    memcpy(pps, pp + st, (size_t)len);
                    *pl = len;
                }
            }
            i = e;
        } else {
            i++;
        }
    }
    return *sl > 0 && *pl > 0;
}

/* gives the decoder, the Engine's image and the library back (what hw_start made, in the other order) */
static void hw_stop(Video *v)
{
    if (v->hwd) {
        hwavc_close(v->hwd);
        v->hwd = NULL;
    }
    if (meboot_current() == MEBOOT_BASELINE) { /* the Engine is given back the image it has for everything else (a Main video, the music's codecs) */
        int br = meboot_start(MEBOOT_MAIN);
        crumbh("hw-boot-main", (unsigned)br, 0, 0, 0);
    }
    hwlib_down();
}

/* Tries the PSP's own decoder for the stream: the library, the Engine's image (Baseline for CAVLC, Main for CABAC and B pictures), the decoder, and the first samples as a test. v->hwd is set when it all
 * worked; else nothing is left running and the way back (the software decoder for CAVLC, the games' library for the rest) goes on. */
static void hw_start(Video *v)
{
    if (v->has_session_options)
        return;
    unsigned char sps[256], pps[256], base[256];
    int sl = 0, pl = 0;
    int can = meboot_available(); /* (looks for the function: the numbers below are only known after it) */
    v->hw_tried = can;
    crumbh("hw-find", meboot_nid(), (unsigned)meboot_devkit(), meboot_mask(), (unsigned)meboot_module()); /* the NID of sceMeBootStart that was found (0: none), the devkit version, which NIDs of the table the library has (bit n: entry n), the module name that found it */
    if (!can || !hw_param_sets(v, sps, &sl, pps, &pl, (int)sizeof(sps))) {
        crumb4("hw-no", can, sl, pl, 0);
        return;
    }
    int boot = v->um.hw_ok && v->opt_hw != 3 ? MEBOOT_BASELINE : MEBOOT_MAIN;
    if (boot == MEBOOT_BASELINE && sl > 4 && sps[1] == 77) { /* a Main label on a stream that is Baseline in what it uses (the HandBrake PSP preset's files): the Engine's Baseline image is given a Baseline SPS */
        int n = h264_sps_to_base(sps, sl, base, (int)sizeof(base));
        if (n > 0) {
            memcpy(sps, base, (size_t)n);
            sl = n;
        }
    }
    hwavc_log(hw_log);
    logs_step(LOG_VIDEO, "the PSP's own decoder: the games' library is let go");
    if (!game_lib_unload())
        return;
    logs_step(LOG_VIDEO, "the PSP's own decoder: its modules load");
    if (hwlib_up() < 0)
        return;
    logs_step(LOG_VIDEO, "the PSP's own decoder: the Media Engine boots");
    int br = meboot_start(boot);
    crumbh("hw-boot", (unsigned)br, meboot_nid(), (unsigned)boot, (unsigned)meboot_current());
    if (br < 0) {
        hwlib_down();
        return;
    }
    logs_step(LOG_VIDEO, "the PSP's own decoder: a session opens");
    HwAvc *h = hwavc_open(sps, sl, pps, pl, mp4_nal_size(v->mp4));
    unsigned char *buf = h ? memalign(64, (size_t)(v->au_cap + 63) & ~63u) : NULL;
    unsigned char *rgb = h ? memalign(64, (size_t)FRAME_W * (size_t)((v->h + 15) & ~15) * 4) : NULL;
    int ok = 0;
    if (h && buf && rgb) { /* the first samples decoded as a test (the decoder is some samples behind: the first gives no picture): every error of the library shows here, before a picture is wanted */
        void *rgbs[HWAVC_PICS] = { rgb, NULL, NULL };
        logs_step(LOG_VIDEO, "the PSP's own decoder: the first samples go in");
        for (int i = 0; i < 12 && i < mp4_video_count(v->mp4); i++) {
            int size = mp4_video_read(v->mp4, i, buf, v->au_cap), pics = 0, w = 0, hh = 0;
            unsigned err = 0;
            int r = size > 0 ? hwavc_decode(h, buf, size, i == 0 ? 3 : 0, rgbs, &pics, &w, &hh, &err) : -9;
            crumb4("hw-probe", i, r, (int)err, pics * 1000000 + w * 1000 + hh); /* the sample, the step that failed (0: none), the library's answer, the pictures and their size */
            if (r != 0)
                break;
            if (pics > 0 && w > 0 && hh > 0) {
                ok = 1;
                break;
            }
        }
    }
    free(rgb);
    if (!ok) {
        free(buf);
        v->hwd = h;
        hw_stop(v); /* (closes h) */
        return;
    }
    v->hwd = h;
    v->hwbuf = buf;
    v->hw_boot = boot;
    v->hw_first = 1;
    hwp_init(&v->hwp, 4);
    crumb("hw-on", boot, 0);
}

/* the games' library (the old route): its modules, sceMpegInit and the memory of the ring buffer and the session. 1 when it is all there, 0 when not, -1 when the library is broken until psp5 is started
 * again (an earlier video ended in its fatal error) */
static int old_route_up(Video *v)
{
    if (v->has_session_options)
        return 0;
    if (s_poisoned)
        return -1;
    logs_step(LOG_VIDEO, "the games' library: its modules load");
    if (!load_modules()) {
        crumb("open-fail", __LINE__, 0);
        return 0;
    }
    if (!v->lib_up) {
        int mi = sceMpegInit();
        VLOG("mpeg init %08x\n", mi);
        if (mi < 0) {
            crumb("open-fail", __LINE__, mi);
            return 0;
        }
        v->lib_up = 1;
    }
    if (!v->ring_data || !v->mpeg_mem) {
        v->ring_size = sceMpegRingbufferQueryMemSize(RING_PACKETS);
        v->mpeg_size = sceMpegQueryMemSize(0);
        if (v->ring_size > 0 && v->mpeg_size > 0) {
            if (!v->ring_data)
                v->ring_data = memalign(64, (size_t)v->ring_size);
            if (!v->mpeg_mem)
                v->mpeg_mem = memalign(64, (size_t)v->mpeg_size);
        }
    }
    if (!v->ring_data || !v->mpeg_mem) {
        VLOG("no memory for the ring buffer\n");
        crumb("open-fail", __LINE__, 0);
        return 0;
    }
    return 1;
}

/* The PSP's own decoder is given up for good and the games' library is the way on (a stream with CABAC or B pictures: the software decoder cannot take it). No thread is running. */
static void hw_to_old_route(Video *v)
{
    hw_stop(v);
    if (v->has_session_options) {
        v->why = "the session policy forbids the hardware and legacy MPEG routes";
        video_mark_failed(v);
        return;
    }
    v->hw_err = 0;
    v->have_pic = 0;
    v->soft = 0;
    v->soft_up = 0;
    v->ge = 0;
    v->pic.psm = GU_PSM_5650;
    v->pic.opaque = 0;
    v->pic.data = v->frame;
    if (old_route_up(v) <= 0) {
        video_mark_failed(v);
        return;
    }
    crumb("hw-old-route", 0, 0);
}

/* gives the PSP's decoder up for good and makes what the software decoder needs (no thread is running: the callers have stopped it or not started it) */
static void hw_to_soft_now(Video *v)
{
    hw_stop(v);
    v->hw_err = 0;
    v->have_pic = 0;
    v->pic.psm = GU_PSM_5650;
    v->pic.opaque = 0;
    if (!soft_buffers(v, (size_t)FRAME_W * (size_t)((v->h + 15) & ~15) * 2)) /* (the picture buffers the PSP's decoder had are bigger) */
        video_mark_failed(v);
    else if (!v->ge)
        v->pic.data = v->fbuf[0];
}

/* The decoder failed while the video played (the screen's thread calls this): the software decoder goes on from where the clock is. */
static void hw_fallback(Video *v, int why)
{
    crumbh("hw-fallback", (unsigned)why, (unsigned)v->n_dec, (unsigned)v->err_total, 0);
    soft_stop(v); /* (the thread first: it uses the decoder) */
    if (v->has_session_options) {
        v->why = "the session policy forbids hardware decoder fallback";
        video_mark_failed(v);
        return;
    }
    if (v->dec)
        hw_to_soft_now(v);
    else
        hw_to_old_route(v); /* (no software decoder for this stream) */
    if (!video_failed(v))
        video_play(v, (float)v->clock);
}

static Video *video_open_internal(const char *path, const VideoOpenOptions *options, VideoOpenStatus *status)
{
    int session_policy = options != NULL;
    VideoOpenStatus result = VIDEO_OPEN_ENGINE_ERROR;
    if (status)
        *status = VIDEO_OPEN_ENGINE_ERROR;
    if (!path) {
        if (status)
            *status = VIDEO_OPEN_INVALID_ARGUMENT;
        return NULL;
    }
    const char *dot0 = strrchr(path, '.');
    int is_pmf = dot0 && (!strcasecmp(dot0, ".pmf") || !strcasecmp(dot0, ".psmf"));
    int is_mp4 = dot0 && !strcasecmp(dot0, ".mp4");
    if (session_policy) {
        if (options->allow_software_h264 != 1 || options->force_rgb565 != 1 ||
            options->allow_hardware_decoder != 0 || options->allow_pmf != 0 ||
            options->allow_legacy_mpeg != 0 || options->allow_yuv != 0) {
            if (status)
                *status = VIDEO_OPEN_INVALID_OPTIONS;
            return NULL;
        }
        if (!is_mp4) {
            if (status)
                *status = VIDEO_OPEN_UNSUPPORTED_CONTAINER;
            return NULL;
        }
    }
    if (s_poisoned && is_pmf)
        return NULL; /* (nothing is written: the file of the video that did it stays) */
    crumb_start();
    crumb("open", 0, 0);
    Video *v = calloc(1, sizeof(*v));
    if (!v) {
        crumb("open-fail", __LINE__, 0);
        crumb_keep("A video could not be opened: not enough memory for the player.");
        if (status)
            *status = VIDEO_OPEN_OUT_OF_MEMORY;
        return NULL;
    }
    v->has_session_options = session_policy;
    if (session_policy)
        v->session_options = *options;
    v->opt_soft = session_policy ? 1 : -1;
    v->opt_ge = session_policy ? 0 : 1;
    v->opt_slow = 1;
    v->opt_hw = session_policy ? 0 : 1; /* (the PSP's own decoder for a stream it may take; hw=0 in video-opts.txt leaves it out) */
    v->speed = 1.0f;
    v->speed_min = 1.0f; /* (a video that was only opened and left has not slowed down: with 0 here its log got an entry every time) */
    const char *dot = strrchr(path, '.');
    int pmf = dot && (!strcasecmp(dot, ".pmf") || !strcasecmp(dot, ".psmf"));
    if (session_policy && pmf) {
        result = VIDEO_OPEN_UNSUPPORTED_CONTAINER;
        goto fail;
    }
    v->frame_dur = 1001.0 / 30000.0; /* (a PMF: the usual 29.97; an MP4 sets its own below) */
    if (pmf) {
        v->kind = VK_PMF;
        v->fp = fopen(path, "rb");
        if (!v->fp || fread(v->header, 1, PKT, v->fp) != PKT || memcmp(v->header, "PSMF", 4))
            { crumb("open-fail", __LINE__, 0); goto fail; }
        v->stream_off = be32(v->header + 8);
        v->stream_size = be32(v->header + 12);
        if (v->stream_off < PKT || (v->stream_off & (PKT - 1)) || v->stream_off > (1 << 20))
            { crumb("open-fail", __LINE__, 0); goto fail; }
        /* the whole head of the file (the entry points can be after the first sector) */
        unsigned char *whole = malloc(v->stream_off);
        if (!whole)
            { crumb("open-fail", __LINE__, 0); goto fail; }
        fseek(v->fp, 0, SEEK_SET);
        if (fread(whole, 1, v->stream_off, v->fp) != v->stream_off) {
            free(whole);
            { crumb("open-fail", __LINE__, 0); goto fail; }
        }
        const unsigned char *e0 = NULL;
        int streams = (whole[0x80] << 8) | whole[0x81];
        for (int i = 0; i < streams && i < 8; i++)
            if ((whole[0x82 + 16 * i] & 0xF0) == 0xE0)
                e0 = whole + 0x82 + 16 * i;
        pmf_header_info(v);
        if (e0 && v->n_ep > 0) {
            unsigned ep_off = be32(e0 + 4);
            v->ep = malloc((size_t)v->n_ep * sizeof(*v->ep));
            if (v->ep)
                for (int i = 0; i < v->n_ep; i++) {
                    const unsigned char *x = whole + ep_off + 10 * (size_t)i;
                    v->ep[i].pts = be32(x + 2);
                    v->ep[i].off = be32(x + 6);
                }
            else
                v->n_ep = 0;
        }
        free(whole);
        v->audio_ok = v->pmf_atrac;
    } else {
        v->kind = VK_MP4;
        v->mp4 = mp4_open(path);
        if (!v->mp4) {
            result = VIDEO_OPEN_UNSUPPORTED_CONTAINER;
            { crumb("open-fail", __LINE__, 0); goto fail; }
        }
        v->w = mp4_width(v->mp4);
        v->h = mp4_height(v->mp4);
        v->duration = mp4_duration(v->mp4);
        v->frame_dur = mp4_video_count(v->mp4) > 0 && v->duration > 0 ? (double)v->duration / mp4_video_count(v->mp4) : 1001.0 / 30000.0;
        v->au_cap = mp4_video_max_size(v->mp4);
        if (v->au_cap <= 0 || v->au_cap > MAX_AU) {
            result = VIDEO_OPEN_UNSUPPORTED_CODEC;
            { crumb("open-fail", __LINE__, 0); goto fail; }
        }
        v->sample = malloc((size_t)v->au_cap);
        v->au[0] = malloc((size_t)v->au_cap + 4096);
        v->au[1] = malloc((size_t)v->au_cap + 4096);
        if (!v->sample || !v->au[0] || !v->au[1]) {
            VLOG("no memory for the samples\n");
            result = VIDEO_OPEN_OUT_OF_MEMORY;
            { crumb("open-fail", __LINE__, 0); goto fail; }
        }
        int opts_n = -1;
        if (!session_policy) {
        { /* test switches (PSP5/video-opts.txt: "synth=0" leaves the PSP's timing out, "main=0" leaves a Baseline SPS as it is, "soft=1" / "soft=0" the software decoder for every stream / for none) */
            char opt[96], buf[128];
            snprintf(opt, sizeof(opt), "%s/video-opts.txt", PSP5_DATA_DIR);
            SceUID fd = sceIoOpen(opt, PSP_O_RDONLY, 0);
            if (fd >= 0) {
                int n = sceIoRead(fd, buf, sizeof(buf) - 1);
                sceIoClose(fd);
                opts_n = n;
                if (n > 0) {
                    buf[n] = 0;
                    v->um.no_synth = strstr(buf, "synth=0") != NULL;
                    v->um.no_main = strstr(buf, "main=0") != NULL;
                    v->um.force = strstr(buf, "force=1") != NULL;
                    v->opt_soft = strstr(buf, "soft=1") ? 1 : strstr(buf, "soft=0") ? 0 : -1;
                    v->opt_hw = strstr(buf, "hw=0") ? 0 : strstr(buf, "hw=3") ? 3 : 1;
                    v->opt_ge = strstr(buf, "ge=0") ? 0 : 1;
                    v->opt_pcm = strstr(buf, "pcm=1") != NULL;
                    v->opt_slow = strstr(buf, "slow=0") == NULL;
                    { const char *bp = strstr(buf, "burn="); v->opt_burn = bp ? atoi(bp + 5) : 0; }
                }
            }
        }
        }
        if (!unit_init(&v->um, v->mp4, v->sample, v->au_cap) || !v->um.params_len) {
            result = session_policy ? VIDEO_OPEN_UNSUPPORTED_CODEC : VIDEO_OPEN_ENGINE_ERROR;
            { crumb("open-fail", __LINE__, 0); goto fail; }
        }
        crumb4("opts", opts_n, v->opt_soft, v->opt_hw, v->opt_ge); /* bytes read from PSP5/video-opts.txt (-1: no such file), soft= (-1 = not given), hw=, ge= */
        int hw_try = !session_policy && v->opt_hw && v->opt_soft != 1 && (v->um.hw_ok || v->um.hw_main); /* (the PSP's own decoder is the first way for a stream it may take) */
        if (v->opt_hw && !(v->um.hw_ok || v->um.hw_main))
            crumb4("hw-not-taken", v->um.profile, v->um.poc, v->um.cabac, v->um.nref); /* the stream is not one the PSP's own decoder is tried with: its profile, picture order type, CABAC, reference frames */
        int soft;
        if (session_policy) {
            soft = v->session_options.allow_software_h264;
        } else {
#ifdef PSP5_ANY_STREAM /* (PPSSPP takes every stream: its pictures are what the tests and the README show; soft=1 tries the software decoder there) */
            soft = v->opt_soft == 1;
#else
            soft = v->um.unsupported ? v->opt_soft != 0 : v->opt_soft == 1;
#endif
        }
        if (soft) { /* the PSP's decoder (the one of the games' library) cannot take the stream (CAVLC, High profile): the PSP's own decoder in its other mode, and h264dec.c as the way back for CAVLC without B pictures */
            int sp = soft_prepare(v);
            if (!sp && session_policy) {
                result = v->soft_why == H264_MEMORY ? VIDEO_OPEN_OUT_OF_MEMORY : VIDEO_OPEN_UNSUPPORTED_CODEC;
                goto fail;
            }
            if (!sp && !(hw_try && v->um.hw_main))
                goto fail;
            if (hw_try)
                hw_start(v);
            if (v->hwd)
                v->soft = 1; /* (the PSP's own decoder runs in the thread the software decoder has) */
            else if (!sp)
                goto fail;
        }
#ifndef PSP5_ANY_STREAM
        else if (v->um.unsupported) { /* (soft=0) refused before the decoder sees it: a stream it cannot take gives its fatal error and breaks it for every video after */
            crumb4("stream", v->um.nref, v->um.wmb * 1000 + v->um.hmb, v->um.cabac * 100 + v->um.weighted * 10 + v->um.to_main, v->um.nref0);
            crumb4("unsupported", v->um.unsupported, v->um.profile, v->um.cabac, 0);
            v->why = v->um.unsupported == 2 ? "a High profile video (8x8 transform) is not played" : "a CAVLC video that the PSP's own decoder was not asked to take";
            goto fail;
        }
#endif
        else if (hw_try && v->um.hw_main) { /* CABAC (or B pictures): the games' library takes it, the PSP's own decoder is tried first; the games' library is the way back */
            hw_start(v);
            if (v->hwd)
                v->soft = 1;
        }
        v->audio_ok = mp4_has_audio(v->mp4);
    }
    VLOG("open %s: %dx%d, %.2f s, kind %d\n", path, v->w, v->h, v->duration, v->kind);
    crumb(v->kind == VK_MP4 ? "mp4" : "pmf", v->w, v->h);
    if (v->kind == VK_MP4 && v->um.params_len > 8 && (v->um.params[4] & 0x1F) == 7) {
        const unsigned char *pp = v->um.params;
        crumb("avc-profile-level", pp[5], pp[7]);
        int n = v->um.params_len < 24 ? v->um.params_len : 24; /* the head of the parameter sets, as they were given to the decoder (profile, sizes, flags) */
        crumbh("params", (unsigned)pp[0] << 24 | pp[1] << 16 | pp[2] << 8 | pp[3], (unsigned)pp[4] << 24 | pp[5] << 16 | pp[6] << 8 | pp[7],
               n >= 12 ? (unsigned)pp[8] << 24 | pp[9] << 16 | pp[10] << 8 | pp[11] : 0, n >= 16 ? (unsigned)pp[12] << 24 | pp[13] << 16 | pp[14] << 8 | pp[15] : 0);
        crumb4("timing", v->um.synth, v->um.inband, (int)v->um.tick.num_units, (int)v->um.tick.time_scale); /* synth 1 = the PSP's timing is written into the stream */
        crumb4("stream", v->um.nref, v->um.wmb * 1000 + v->um.hmb, v->um.cabac * 100 + v->um.weighted * 10 + v->um.to_main, v->um.nref0); /* reference frames, size in macroblocks, CABAC / weighted prediction / relabelled, default reference pictures */
    }
    if (v->w <= 0 || v->h <= 0 || v->w > FRAME_W || v->h > 512) {
        result = session_policy ? VIDEO_OPEN_UNSUPPORTED_OUTPUT : VIDEO_OPEN_ENGINE_ERROR;
        crumb("open-fail", __LINE__, 0);
        v->why = "the picture is bigger than 512 x 512";
        goto fail;
    }
    if (session_policy && (!v->soft || v->hwd || v->ge)) {
        result = VIDEO_OPEN_UNSAFE_ROUTE;
        v->why = "the file cannot use the software RGB565 decoder route";
        goto fail;
    }
    if (!v->soft) { /* the games' library: its modules, its memory (not touched by a video that is decoded in software or by the PSP's own decoder) */
        if (session_policy) {
            result = VIDEO_OPEN_UNSAFE_ROUTE;
            v->why = "the session policy forbids the legacy MPEG route";
            goto fail;
        }
#ifndef PSP5_ANY_STREAM
        /* The games' library breaks on a stream with three or more reference frames (the maintainer's tests of 2026-11-08: one or two played, three failed; the log of 2026-11-15, a 4-reference x264 stream the
         * PSP's own decoder had refused too: first-decode 0x80628002, then 0x80628001, the fatal error that stops every video after it until psp5 is started again). Such a stream is refused here, before
         * the library sees it: "can't be played", and the next video plays. (An SPS that is not given to the decoder, nref 0, is not judged; force=1 in video-opts.txt tries it anyway.) */
        if (v->kind == VK_MP4 && v->um.nref > 2 && !v->um.force) {
            crumb4("old-route-refused", v->um.nref, v->um.cabac, v->um.weighted, v->um.nref0);
            v->why = "the PSP's own decoder did not take a stream with so many reference frames, and the games' library cannot play it either";
            goto fail;
        }
#endif
        int up = old_route_up(v);
        if (up < 0)
            goto refuse;
        if (!up)
            goto fail;
    }
    /* the decoder writes whole blocks of 16 and the PSP's own library decides how far it goes: the buffer is always the biggest a video can need (512 by 512 of 16 bits), so nothing can be written past it */
    size_t frame_bytes = (size_t)FRAME_W * (v->hwd ? (size_t)((v->h + 15) & ~15) * 4 : 512 * 2); /* (RGBA from the PSP's own decoder, which writes whole rows of blocks of 16: no more is needed; memory is short) */
    v->frame = memalign(64, frame_bytes);
    if (!v->frame) {
        result = VIDEO_OPEN_OUT_OF_MEMORY;
        VLOG("no memory for the picture\n");
        { crumb("open-fail", __LINE__, 0); goto fail; }
    }
    memset(v->frame, 0, frame_bytes);
    sceKernelDcacheWritebackInvalidateRange(v->frame, (unsigned)frame_bytes);
    v->pic.w = v->w;
    v->pic.h = v->h;
    v->pic.tw = FRAME_W;
    v->pic.th = pow2(v->h);
    v->pic.stride = FRAME_W;
    v->pic.psm = v->hwd ? GU_PSM_8888 : GU_PSM_5650;
    v->pic.opaque = v->hwd != NULL; /* (the converter's alpha is not known: the picture is drawn without blending) */
    v->pic.data = v->frame;
    if (v->soft && !soft_buffers(v, frame_bytes)) {
        result = VIDEO_OPEN_OUT_OF_MEMORY;
        crumb("open-fail", __LINE__, 0);
        goto fail;
    }
    if (session_policy && (!v->soft || v->hwd || v->ge || v->pic.psm != GU_PSM_5650)) {
        result = VIDEO_OPEN_UNSUPPORTED_OUTPUT;
        v->why = "the decoder did not provide software RGB565 output";
        goto fail;
    }
    v->pic.vram = 1; /* (the picture's memory is the engine's: gfx never frees it) */
    /* the sound: the queue and its thread, and the decoder of an MP4's AAC */
    if (v->audio_ok) {
        int rate = v->kind == VK_MP4 ? mp4_audio_rate(v->mp4) : 44100;
        crumb("sound-open", rate, v->kind == VK_MP4);
        if (v->kind == VK_MP4 && !aac_open(v))
            v->audio_ok = 0; /* (the sound is not one aac.c can decode: the video plays without it) */
        crumb("sound-out", rate, v->audio_ok);
        if (v->audio_ok) {
            int audio_started = audio_start(v, rate);
            if (audio_started < 0 && session_policy) {
                result = VIDEO_OPEN_THREAD_ERROR;
                v->why = "could not start the video audio thread";
                goto fail;
            }
            if (audio_started <= 0)
                v->audio_ok = 0; /* audio is optional for the legacy open path */
        }
    }
    if (v->audio_ok && v->opt_pcm) { /* test switch: keep the first 8 seconds of the sound */
        v->dump_cap = (unsigned)v->rate * 4 * 8;
        v->dump = malloc(v->dump_cap);
    }
    v->has_sound = v->audio_ok;
    crumb("opened", v->has_sound, 0);
    if (status)
        *status = VIDEO_OPEN_OK;
    return v;
refuse:
    v->quiet = 1; /* (the PSP's decoder is broken until psp5 is started again, and this stream needs it) */
    result = VIDEO_OPEN_ENGINE_ERROR;
    video_close(v);
    if (status)
        *status = result;
    return NULL;
fail:
    VLOG("open failed\n");
    video_mark_failed(v);
    if (!v->why) {
        if (result == VIDEO_OPEN_UNSUPPORTED_CODEC)
            v->why = "the H.264 software decoder cannot handle this file";
        else if (result == VIDEO_OPEN_UNSUPPORTED_OUTPUT)
            v->why = "the video output is not supported by this session policy";
        else if (result == VIDEO_OPEN_UNSAFE_ROUTE)
            v->why = "the video would require a forbidden decoder route";
        else if (result == VIDEO_OPEN_OUT_OF_MEMORY)
            v->why = "not enough memory for the video";
        else if (result == VIDEO_OPEN_UNSUPPORTED_CONTAINER)
            v->why = "the file is not a supported MP4 container";
    }
    video_close(v);
    if (status)
        *status = result;
    return NULL;
}

Video *video_open(const char *path)
{
    return video_open_internal(path, NULL, NULL);
}

VideoOpenStatus video_open_ex(const char *path, const VideoOpenOptions *options, Video **out_video)
{
    if (out_video)
        *out_video = NULL;
    if (!path || !options || !out_video)
        return VIDEO_OPEN_INVALID_ARGUMENT;
    VideoOpenStatus status = VIDEO_OPEN_ENGINE_ERROR;
    Video *v = video_open_internal(path, options, &status);
    *out_video = v;
    return status;
}

void video_close(Video *v)
{
    if (!v)
        return;
    if (!v->quiet)
        crumb("closing", 0, 0);
    soft_stop(v);
    int hw_good = v->hwd != NULL && !v->hw_err; /* (the PSP's decoder played it to the end: nothing to look at) */
    if (v->hwd)
        logs_step(LOG_VIDEO, "closing: the PSP's own decoder is let go");
    hw_stop(v);
    v->started = 0;
    audio_stop(v);
    session_destroy(v);
    free(v->ring_data);
    free(v->mpeg_mem);
    if (v->lib_up && TD_STEP(TD_FINISH))
        sceMpegFinish();
    aac_close(v);
    h264d_free(v->dec);
    if (v->soft && !v->quiet)
        crumb4("soft-end", (int)v->soft_skip_n, (int)v->soft_late_n, (int)(v->soft_us / 1000), 0); /* pictures made without the loop filter, pictures that were late, milliseconds of decoding since the last jump */
    if (v->quiet) {
        crumb_end();
    } else {
        crumb4("end", v->n_put, v->n_au, v->n_dec, v->err_total);
        int slow = v->soft && (v->soft_late_n * 5 > (unsigned)v->n_dec + 5 || v->speed_min < 0.97f); /* (a video in software that was late with more than one picture in five, or had to slow down: how fast the PSP is) */
        if (video_failed(v) || v->err_total > 0 || (v->hw_tried && !hw_good) || slow) {
            char facts[100], why[360];
            if (v->kind == VK_MP4 && v->w > 0)
                snprintf(facts, sizeof(facts), "MP4 %dx%d, profile %d, %d reference frames, %s", v->w, v->h, v->um.profile, v->um.nref, v->um.cabac ? "CABAC" : "CAVLC");
            else if (v->kind == VK_MP4)
                snprintf(facts, sizeof(facts), "the file could not be read as an MP4");
            else
                snprintf(facts, sizeof(facts), "PMF / PSMF");
            if (video_failed(v))
                snprintf(why, sizeof(why), "The video could not be played%s%s (%s).", v->why ? ": " : "", v->why ? v->why : "", facts);
            else if (v->err_total > 0)
                snprintf(why, sizeof(why), "The video played, but the decoder gave %d errors (%s).", v->err_total, facts);
            else if (v->hw_tried && !hw_good)
                snprintf(why, sizeof(why), "The PSP's own decoder was tried and did not play the video to the end (%s).", facts);
            else
                snprintf(why, sizeof(why), "The software decoder was too slow for this video: %u of %d pictures were late and the speed went down to %d%% (%s).", v->soft_late_n, v->n_dec, (int)(v->speed_min * 100.0f), facts);
            crumb_keep(why);
        } else {
            crumb_end(); /* it was closed the right way and played: nothing to leave behind */
        }
    }
    if (v->dump && v->dump_n) {
        char path[96];
        snprintf(path, sizeof(path), "%s/video-pcm.raw", PSP5_DATA_DIR);
        SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
        if (fd >= 0) {
            sceIoWrite(fd, v->dump, v->dump_n);
            sceIoClose(fd);
        }
    }
    free(v->dump);
    free(v->hwbuf);
    free(v->ybuf[0]);
    free(v->ybuf[1]);
    free(v->ybuf[2]);
    if (v->fbuf[1] != v->frame)
        free(v->fbuf[1]);
    if (v->fbuf[2] != v->frame)
        free(v->fbuf[2]);
    if (v->fbuf[3] != v->frame)
        free(v->fbuf[3]);
    free(v->frame);
    free(v->ep);
    free(v->sample);
    free(v->au[0]);
    free(v->au[1]);
    if (v->fp)
        fclose(v->fp);
    mp4_close(v->mp4);
    free(v);
}

int video_width(const Video *v) { return v->w; }
int video_height(const Video *v) { return v->h; }
float video_duration(const Video *v) { return v->duration; }
int video_has_sound(const Video *v) { return v->has_sound; }
int video_ended(const Video *v) { return v->ended; }
int video_failed(const Video *v)
{
    if (!v)
        return 1;
    return __sync_val_compare_and_swap((volatile int *)&v->failed, 0, 0);
}
const char *video_error(const Video *v)
{
    if (!v)
        return NULL;
    (void)video_failed(v); /* acquire the worker's published failure reason, when present */
    return v->why;
}
const VideoFrame *video_picture(const Video *v) { return v && !video_failed(v) && v->have_pic && !v->ge ? &v->pic : NULL; }
int video_soft(const Video *v) { return v->soft && v->soft_up; }
const VideoYuv *video_yuv(const Video *v) { return v->have_pic && v->ge ? &v->yuv[v->front] : NULL; }

int video_play(Video *v, float at)
{
    if (!v || video_failed(v))
        return 0;
    if (v->has_session_options && (!v->soft || v->hwd || v->ge || v->pic.psm != GU_PSM_5650)) {
        v->why = "the session policy requires software RGB565 output";
        video_mark_failed(v);
        return 0;
    }
    if (at < 0)
        at = 0;
    if (v->duration > 0 && at > v->duration - 0.5f)
        at = v->duration > 1 ? v->duration - 1 : 0;
    if (v->soft) {
        soft_stop(v); /* (the thread first: it reads the file and the clock) */
        if (at > 0)
            at = soft_pick_start(v, at);
    }
    /* stop what runs: the sound is emptied, the session of sceMpeg is made again */
    v->started = 0;
    v->clock_audio = 0;
    if (v->audio_ok || v->pcm) {
        v->reset = 1;
        for (int i = 0; i < 100 && v->reset && v->athread > 0; i++)
            sceKernelDelayThread(3000);
        v->head = v->tail = 0;
        v->played = 0;
    }
    session_destroy(v);
    v->soft_up = 0;
    v->src_eof = 0;
    v->have_au = 0;
    v->have_ts = 0;
    if (!v->soft)
        v->have_pic = 0; /* (a picture made in software stays on the screen until the run up to the new place is done) */
    v->ended = 0;
    v->no_data = 0;
    v->audio_dry = 0;
    v->dec_errors = 0;
    v->stream_error = 0;
    v->stage = 0;
    v->decoded = 0;
    v->n_put = v->n_au = v->n_dec = 0;
    v->soft_us = v->soft_late_n = v->soft_skip_n = 0;
    v->t_io = v->t_conv = v->t_aud = v->underruns = 0;
    v->gap_max = v->gap_late = v->blocks = 0;
    v->aud_errors = 0;
    v->n_seen = v->hb_lines = 0;
    v->hb = v->since = 0;
    v->a_has_base = 0;
    v->atrac_first = 1;
    v->idle = 0;
    v->t_play0 = now_us();
    v->speed = 1.0f;
    v->speed_min = 1.0f;
    v->ctl_t = 0;
    v->lf_block = 0;
    v->clock = at;
    v->clock_ms = (int)(at * 1000.0);
    v->skip_until = at;
    if (v->kind == VK_MP4) {
        v->vnext = mp4_video_at(v->mp4, at);
        v->anext = v->audio_ok ? mp4_audio_at(v->mp4, at) : 0;
        if (v->aac)
            aac_flush(v->aac); /* (the sound of the frame before is not the one that comes now) */
        v->au_sel = 0;
        if (!v->soft)
            ps_init(&v->ps, v->vnext, mp4_video_count(v->mp4), mp4_size, mp4_pull, v);
    } else {
        unsigned off = 0;
        for (int i = 0; i < v->n_ep; i++) { /* the last entry point at or before the time */
            if ((double)(v->ep[i].pts - v->first_ts) / 90000.0 > at)
                break;
            off = v->ep[i].off;
        }
        if (at <= 0.01f)
            off = 0;
        fseek(v->fp, (long)(v->stream_off + (unsigned long)off * PKT), SEEK_SET);
    }
    int ok = v->soft ? soft_start(v) : session_create(v);
    VLOG("play at %.2f: session %d\n", at, ok);
    if (!ok) {
        if (!v->why)
            v->why = "could not start the video decoder thread or MPEG session";
        video_mark_failed(v); /* (the log gets an entry when the video is closed) */
    } else
        logs_step_later(LOG_VIDEO, "playing"); /* (what the mark of a PSP that stops now says: not the step of the start, which has ended well) */
    return ok;
}

void video_pause(Video *v, int paused)
{
    v->paused = paused;
}

float video_time(const Video *v)
{
    double t = v->clock;
    if (t < 0)
        t = 0;
    if (v->duration > 0 && t > v->duration)
        t = v->duration;
    return (float)t;
}

/* ---------------------------------------------------------------------- */
/* the pictures of a stream the PSP's decoder cannot take: h264dec.c (see h264dec.h) */

#define SOFT_BUDGET_US 45000 /* the time one call of video_update decodes for at most: the screen and the controls are served between the calls */

/* Where a jump to `at` starts. A picture can only be decoded after the ones it is predicted from, so a jump starts at the sync sample before it and decodes up to `at`; with the 8 seconds between the sync
 * samples of an ordinary video and a decoder that is not much faster than the video itself that is a long wait. When the sync sample after `at` is closer than the one before it and the run up from the
 * one before is longer than two seconds, the jump goes to the one after: the time of the video is then that of the sync sample. */
static float soft_pick_start(Video *v, float at)
{
    int k = mp4_video_at(v->mp4, at), n = mp4_video_count(v->mp4);
    float d0, p0;
    int key;
    mp4_video_times(v->mp4, k, &d0, &p0, &key);
    if (at - p0 <= 2.0f)
        return at;
    for (int j = k + 1; j < n && j < k + 1500; j++) {
        float d, p;
        mp4_video_times(v->mp4, j, &d, &p, &key);
        if (key) {
            if (p - at < at - p0)
                return p;
            break;
        }
    }
    return at;
}

/* Publishes the picture that is waiting to be made into RGB: into the buffer that is neither on the screen nor ready (three buffers: the screen loop takes `rdy` and makes it `front`, always in that order, and
 * this thread looks at `rdy` and then at `front`, so it never writes into one the screen has or is about to take). Not a picture from before the place a jump asked for. */
static void soft_publish(Video *v)
{
    if (!v->pending)
        return;
    if (v->has_session_options && (!v->soft || v->hwd || v->ge || v->pic.psm != GU_PSM_5650)) {
        v->pending = 0;
        v->why = "the decoder produced a forbidden output format";
        video_mark_failed(v);
        return;
    }
    v->pending = 0;
    if (v->pend_ts + 0.034 < v->skip_until - 0.03)
        return;
    int rd = v->rdy, fr = v->front, b = 0;
    while (b == rd || b == fr)
        b++;
    unsigned long long a = now_us();
    if (v->ge) { /* the planes: the graphics chip makes the colours (and reads the memory itself: written out of the cache) */
        h264d_copy_planes(v->dec, (unsigned char *)v->yuv[b].plane[0], 512, (unsigned char *)v->yuv[b].plane[1], (unsigned char *)v->yuv[b].plane[2], 256);
        sceKernelDcacheWritebackInvalidateRange(v->ybuf[b], (unsigned)(ysz_of(v->h) + 2 * csz_of(v->h)));
    } else {
        h264d_rgb565(v->dec, (uint16_t *)v->fbuf[b], FRAME_W, b);
        sceKernelDcacheWritebackInvalidateRange(v->fbuf[b], (unsigned)(v->h * FRAME_W * 2));
    }
    v->t_conv += (unsigned)(now_us() - a);
    v->last_pub = now_us();
    v->rdy = b;
}

/* decodes the next picture when its time has come (the clock's or before it); 1 when it did something, 0 when nothing is due */
/* the speed: when the pictures are late the video is played slower (the sound with it) until they are not, and a little faster again when they have caught up; late is how late the picture is that has just
 * been looked at, 0 for the run up from a key frame to the place of a jump (that is not the decoder being slow) */
static void speed_ctl(Video *v, double late_ctl)
{
    if (!v->opt_slow)
        return;
    unsigned long long tn = now_us();
    if (!v->ctl_t)
        v->ctl_t = tn;
    if (tn - v->ctl_t < 100000)
        return;
    float dts2 = (float)(tn - v->ctl_t) / 1e6f, sp = v->speed;
    v->ctl_t = tn;
    if (late_ctl > 0.04)
        sp -= 0.30f * (float)(late_ctl - 0.04) * dts2;
    else if (late_ctl < 0.02)
        sp += 0.03f * dts2;
    sp = sp < 0.7f ? 0.7f : sp > 1.0f ? 1.0f : sp;
    v->speed = sp;
    if (sp < 0.995f)
        v->lf_block = tn + 15000000ull; /* (the loop filter is not used again for 15 seconds after the speed was below one: with it a picture takes a third longer, and the speed would go down again) */
    if (sp < v->speed_min)
        v->speed_min = sp;
}

/* a new place inside a play (the picture is far behind and the play goes on at a later key frame): the PSP's decoder starts from nothing, as at every jump */
static void hw_discontinuity(Video *v)
{
    hwp_clear(&v->hwp);
    v->hw_first = 1;
    logs_step_later(LOG_VIDEO, "a jump: the PSP's own decoder starts again");
    int rr = hwavc_reset(v->hwd);
    crumbh("hw-reset", (unsigned)rr, 1, 0, 0);
    if (rr != 0)
        v->hw_err = 1;
}

/* One turn of the worker thread for a stream the PSP's own decoder makes the pictures of: what to do is hwpipe.c's (show the picture that is due, give the decoder a sample when no picture waits, push the
 * pictures out at the end); this does it. */
static int hw_step(Video *v)
{
    if (v->hw_err || v->read_err)
        return 0; /* (the screen's thread is about to stop this thread and go on with the software decoder, or to end the video) */
    HwPipe *p = &v->hwp;
    int n = mp4_video_count(v->mp4);
    double clock = v->clock_ms / 1000.0;
    speed_ctl(v, v->hw_late);
    HwAct act = hwp_act(p, clock, n - v->vnext);
    if (act == HWP_WAIT) {
        v->hw_late = 0; /* (the decoder is ahead of the sound: not late) */
        return 0;
    }
    if (act == HWP_END) {
        if (!(v->src_eof & 1))
            crumb4("hw-end", v->hw_pub, n - v->hw_from, p->npend, p->flush); /* pictures shown, samples given since the play started, times still waiting (pictures the decoder did not give), key frames given again to push the last ones out */
        v->src_eof |= 1;
        v->hw_late = 0; /* (nothing is left to be late: the speed goes back up) */
        return 0;
    }
    if (act == HWP_PUBLISH) { /* the picture's time has come */
        HwOut o = hwp_pop_out(p);
        double late = clock - o.pts;
        int run_up = o.pts < v->skip_until + 1.0; /* (the first second after a place that a jump or a resume asked for: the decoder had to run up to it from a key frame, and these pictures are late because of that, not because it is slow: the sound is not slowed for them, and the crash file is not kept for them) */
        v->hw_late = late > 0 && !run_up ? late : 0;
        v->soft_late_n += late > 0.1 && !run_up;
        v->rdy = o.buf; /* (the screen's thread takes it) */
        v->hw_pub++;
        v->last_pub = now_us();
        v->since = 0;
        return 1;
    }
    /* a sample goes to the decoder: the next of the video, or (at the end) a key frame again to push the pictures out */
    int i, key = 1, mode = 3;
    double pts = -1;
    if (act == HWP_FEED) {
        float dts, pts_f;
        mp4_video_times(v->mp4, v->vnext, &dts, &pts_f, &key);
        pts = pts_f;
        double late = clock - pts;
        if (late > 1.0) { /* far behind (the run up of a jump from a far key frame): on to a key frame that is due, or comes within a second, skipping what is between */
            int k = mp4_video_at(v->mp4, (float)(clock + 1.0));
            if (k > v->vnext) {
                crumb4("soft-jump", v->vnext, k, (int)(late * 1000), 1);
                v->vnext = k;
                hw_discontinuity(v);
                return 1;
            }
        }
        i = v->vnext++;
        mode = (key || v->hw_first) ? 3 : 0;
    } else {
        i = 0; /* (the first sample of the video is a key frame) */
        if (!v->hw_flushing) {
            v->hw_flushing = 1;
            crumb4("hw-flush", p->npend, 0, 0, 0); /* the pictures the decoder holds at the end */
        }
    }
    unsigned long long t0 = now_us();
    int sz = mp4_video_read(v->mp4, i, v->hwbuf, v->au_cap);
    v->t_io += (unsigned)(now_us() - t0);
    if (sz <= 0 && act != HWP_FLUSH) { /* the sample cannot be read: no decoder can do anything with it, and a way on that starts again from the file (the games' library) crashed a PSP that had slept */
        crumb4("read-error", sz, i, v->n_dec, 0);
        v->err_total++;
        v->read_err = 1;
        return 0;
    }
    int dst[HWP_PIC], pics = 0, ww = 0, hh = 0, hr = -9;
    unsigned err = 0;
    hwp_plan(p, pts, v->skip_until, v->front, v->rdy, dst);
    void *rgb[HWAVC_PICS];
    for (int k = 0; k < HWAVC_PICS; k++)
        rgb[k] = dst[k] >= 0 ? v->fbuf[dst[k]] : NULL;
    unsigned long long a = now_us();
    if (sz > 0)
        hr = hwavc_decode(v->hwd, v->hwbuf, sz, mode, rgb, &pics, &ww, &hh, &err);
    unsigned took = (unsigned)(now_us() - a);
    v->soft_us += took;
    v->n_au++;
    v->n_dec++;
    v->n_put = (int)(v->soft_us / (unsigned)v->n_dec); /* (the first number of the log's "alive" and "end": the average microseconds a sample took) */
    if (v->n_dec == 1) {
        crumbh("first-decode", (unsigned)hr, (unsigned)sz, took, (unsigned)pics);
        v->stage |= 2;
    }
    if (hr != 0) {
        if (act == HWP_FLUSH) { /* (the end of the video: what the decoder holds is not worth an error) */
            crumb4("hw-flush-error", hr, (int)err, p->npend, 0);
            p->npend = 0;
            return 1;
        }
        crumb4("hw-error", hr, (int)err, i, v->n_dec); /* the step that failed, the library's answer, the sample, the samples before it */
        v->err_total++;
        v->hw_err = 1;
        return 1;
    }
    hwp_done(p, pics, dst, v->skip_until);
    if (act == HWP_FEED)
        v->hw_first = 0;
    v->dec_errors = 0;
    v->since = 0;
    return 1;
}

static int soft_step(Video *v)
{
    if (v->hwd)
        return hw_step(v);
    int n = mp4_video_count(v->mp4);
    if (v->vnext >= n) {
        v->src_eof |= 1;
        soft_publish(v);
        return 0;
    }
    float dts, pts_f;
    int key;
    mp4_video_times(v->mp4, v->vnext, &dts, &pts_f, &key);
    double pts = pts_f, clock = v->clock_ms / 1000.0, late = clock - pts;
    speed_ctl(v, pts < v->skip_until - 0.03 ? 0.0 : late); /* (the run up from a key frame to the place of a jump is not the decoder being slow: the speed does not go down for it) */
    if (pts > clock + 0.004) {
        soft_publish(v); /* (caught up: what was made last goes to the screen) */
        return 0;
    }
    if (late > 1.0) { /* far behind (the decoder is slower than the video): on to a sync sample that is due, or comes within a second (the picture stands still till then), skipping what is between */
        int k = mp4_video_at(v->mp4, (float)(clock + 1.0));
        if (k > v->vnext) {
            crumb4("soft-jump", v->vnext, k, (int)(late * 1000), 0);
            v->vnext = k;
            h264d_reset(v->dec);
            v->pending = 0;
            return 1;
        }
    }
    int i = v->vnext++;
    unsigned long long t0 = now_us();
    int sz = mp4_video_read(v->mp4, i, v->sample, v->au_cap);
    v->t_io += (unsigned)(now_us() - t0);
    /* a picture that is late is made without the loop filter (what a later picture is predicted from is then not quite as it should be: about the cheapest way to catch up); so are the long run-ups of a jump */
    int fast = late > 0.1 || v->skip_until - pts > 0.5 || (v->opt_slow && (v->speed < 0.995f || now_us() < v->lf_block));
    v->soft_late_n += late > 0.1;
    h264d_set_fast(v->dec, fast);
    v->soft_skip_n += (unsigned)fast;
    unsigned long long a = now_us();
    int r = sz > 0 ? h264d_decode(v->dec, v->sample, sz) : -1;
#ifdef PSP5_TEST
    if (v->opt_burn > 0) { /* test switch burn=N: N more milliseconds of work for every picture (a slower processor) */
        unsigned long long until = now_us() + (unsigned long long)v->opt_burn * 1000;
        while (now_us() < until) {}
    }
#endif
    unsigned took = (unsigned)(now_us() - a);
    v->soft_us += took;
    v->n_au++;
    v->n_dec++;
    v->n_put = (int)(v->soft_us / (unsigned)v->n_dec); /* (the log's first number of "alive" and "end" is then the average microseconds a picture took) */
    if (v->n_dec == 1) {
        crumbh("first-decode", (unsigned)r, (unsigned)sz, took, (unsigned)h264d_why(v->dec));
        v->stage |= 2;
    }
    if (r < 0) {
        v->err_total++;
        if (v->dec_errors < 3)
            crumb4("decode-error", h264d_why(v->dec), v->dec_errors, sz, i);
        if (++v->dec_errors > 30)
            video_mark_failed(v);
        return 1;
    }
    v->dec_errors = 0;
    v->since = 0; /* (it goes on: the run up to a far place can take longer than the watchdog's eight seconds) */
    v->last_ts = pts;
    v->have_ts = 1;
    if (r > 0) {
        v->pending = 1;
        v->pend_ts = pts;
    }
    if (v->pending) { /* made into RGB now, unless the next picture is due as well (this one would hardly be seen) and a picture went to the screen lately */
        int more = 0;
        if (v->vnext < n) {
            float d2, p2;
            mp4_video_times(v->mp4, v->vnext, &d2, &p2, &key);
            more = p2 <= clock + 0.004;
        }
        if (!more || now_us() - v->last_pub > 120000)
            soft_publish(v);
    }
    return 1;
}

/* the thread of the software decoder: the sound is kept filled, and the pictures that are due are made */
static int soft_thread(SceSize args, void *argp)
{
    Video *v = *(Video **)argp;
    while (!v->wquit && !video_failed(v)) {
        if (v->paused) {
            sceKernelDelayThread(10000);
            continue;
        }
        pump_audio(v);
        if (!soft_step(v))
            sceKernelDelayThread(3000);
    }
    return 0;
}

static void soft_stop(Video *v)
{
    v->wquit = 1;
    if (v->wthread > 0) {
        sceKernelWaitThreadEnd(v->wthread, NULL);
        sceKernelDeleteThread(v->wthread);
        v->wthread = 0;
    }
    v->soft_up = 0;
}

static int soft_start(Video *v)
{
    if (v->dec)
        h264d_reset(v->dec);
    v->pending = 0;
    v->rdy = -1;
    v->last_pub = 0;
    v->hw_first = 1; /* (a play starts at a key frame: the PSP's decoder is given it as a first sample) */
    v->hw_err = 0;
    hwp_clear(&v->hwp);
    v->hw_late = 0;
    v->hw_flushing = 0;
    v->hw_pub = 0;
    v->hw_from = v->vnext;
    if (v->hwd) { /* a new place: the decoder keeps pictures and state a key frame does not clear (after a jump the first version of this froze for seconds), so it is opened again, as PMPlayer Ultraviolet does at every seek */
        int rr = hwavc_reset(v->hwd);
        crumbh("hw-reset", (unsigned)rr, 0, 0, 0);
        if (rr != 0)
            v->hw_err = 1; /* (it will not open again: the thread starts and does nothing, the screen's thread sees the error and the way back goes on from the clock) */
    }
    v->wquit = 0;
    v->wthread = sceKernelCreateThread("psp5_vdec", soft_thread, 0x24, 0x10000, 0, NULL); /* (below the screen's thread: the buttons and the drawing come first) */
    if (v->wthread < 0) {
        v->wthread = 0;
        v->soft_up = 0;
        v->why = "could not create the software video thread";
        return 0;
    }
    Video *self = v;
    int start = sceKernelStartThread(v->wthread, sizeof(self), &self);
    if (start < 0) {
        sceKernelDeleteThread(v->wthread); /* it never ran: do not wait for it */
        v->wthread = 0;
        v->soft_up = 0;
        v->wquit = 1;
        v->why = "could not start the software video thread";
        return 0;
    }
    v->soft_up = 1;
    return 1;
}

int video_update(Video *v, float dt)
{
    if (!v)
        return 0;
    if (v->has_session_options && (!v->soft || v->hwd || v->ge || v->pic.psm != GU_PSM_5650)) {
        v->why = "the session policy requires software RGB565 output";
        video_mark_failed(v);
        return 0;
    }
    if (!(v->soft ? v->soft_up : v->mpeg_up) || video_failed(v))
        return 0;
    if (v->paused)
        return 0;
    if (!v->soft) {
        for (int i = 0; i < 3; i++)
            feed(v);
        pump_audio(v); /* (a stream in software has the thread keep its sound filled) */
    }
    /* the clock: the sound that has been played, or the time that has gone when there is no sound */
    if (v->audio_ok && v->clock_audio && v->a_has_base && !((v->src_eof & 2) && pcm_count(v) == 0))
        v->clock = v->a_base + (double)v->played / v->rate;
    else if (!v->audio_ok || v->idle > 1.0f || (v->started && (v->src_eof & 2))) /* (no sound, none came, or it has all been played: the time goes by itself) */
        v->clock += (v->soft ? dt * v->speed : dt); /* (no sound, or none came in a second: the picture does not wait for it) */
    else
        v->idle += dt;
    v->clock_ms = (int)(v->clock * 1000.0);
    /* signs of life, ten seconds apart: a note or two in memory (they come with the entry of a failure), and the mark of the step that is going on is made new (written by the thread: the screen's thread
     * does not wait for the card), so that a PSP that stops while a video plays leaves how far it had come; and the end of a session that gives no picture at all (the screen would stay black for ever) */
    v->hb += dt;
    if (v->hb >= 10.0f) {
        v->hb = 0;
        v->hb_lines++;
        crumb4("alive", v->n_put, v->n_au, v->n_dec, (int)(v->clock * 1000));
        if (v->soft)
            crumb4("soft-spd", (int)(v->speed * 1000), (int)(v->speed_min * 1000), (int)v->soft_late_n, (int)v->soft_skip_n); /* how fast the video plays now and at the slowest (1000 = as it is), pictures that were late, pictures made without the loop filter */
        logs_step_later(LOG_VIDEO, "playing");
    }
    if (!v->have_pic && (v->since += dt) > 8.0f) {
        crumb4("no-picture", v->n_put, v->n_au, v->n_dec, v->src_eof);
        if (v->hwd) { /* (the PSP's decoder gave nothing in eight seconds: the software decoder goes on) */
            hw_fallback(v, -4);
            return 0;
        }
        v->why = "no picture came in eight seconds";
        video_mark_failed(v);
        return 0;
    }
    if (v->soft) { /* the thread makes the pictures: what is ready goes on the screen */
        if (v->read_err && v->hwd) { /* the file cannot be read: the video ends here (the thread is stopped first: it uses the decoder) */
            soft_stop(v);
            v->why = "a sample could not be read from the card (the card was taken out, or the PSP slept)";
            video_mark_failed(v);
            return 0;
        }
        if (v->hw_err && v->hwd) { /* the PSP's decoder did not take a sample: the software decoder goes on from here */
            hw_fallback(v, 1);
            return 0;
        }
        int shown = 0, r = v->rdy;
        if (r >= 0) {
            if (v->has_session_options && (!v->fbuf[r] || v->pic.psm != GU_PSM_5650)) {
                v->rdy = -1;
                v->why = "the software decoder did not produce RGB565 output";
                video_mark_failed(v);
                return 0;
            }
            v->front = r; /* (in this order: see soft_publish) */
            if (!v->ge)
                v->pic.data = v->fbuf[r];
            v->rdy = -1;
            v->have_pic = 1;
            shown = 1;
        }
        if (v->duration > 0 && v->clock > v->duration + 0.5 && (v->src_eof & 1))
            v->ended = 1;
        return shown;
    }
    if (v->has_session_options) {
        v->why = "the session policy blocked entry to sceMpegAvcDecode";
        video_mark_failed(v);
        return 0;
    }
    int shown = 0;
    for (int n = 0; n < 4; n++) {
        if (!v->have_au) {
            SceInt32 unk = 0;
            unsigned r = (unsigned)sceMpegGetAvcAu(&v->mpeg, v->s_avc, &v->au_avc, &unk);
            if (r != 0) { /* the first different answers other than "a unit": what a PSP says when it gives none */
                int known = 0;
                for (int i = 0; i < v->n_seen; i++)
                    known |= v->seen[i] == r;
                if (!known && v->n_seen < 6) {
                    v->seen[v->n_seen++] = r;
                    crumb4("get-au", (int)r, v->n_put, v->n_au, v->src_eof);
                }
            }
            if (r == NO_DATA) {
                feed(v);
                if ((v->src_eof & 1) && ++v->no_data > 20)
                    v->ended = 1;
                break;
            }
            if (r != 0) {
                VLOG("get au %08x\n", r);
                v->err_total++;
                if (r == 0x80020199u)
                    s_poisoned = 1; /* (an unknown semaphore: what a PSP says to every video after a fatal error of the decoder) */
                if (++v->stream_error > 30)
                    video_mark_failed(v);
                break;
            }
            v->no_data = 0;
            v->have_au = 1;
            v->n_au++;
            if (v->n_au <= 4)
                crumbh("unit-ts", v->au_avc.iPtsMSB, v->au_avc.iPts, v->au_avc.iDtsMSB, v->au_avc.iDts);
            /* The time the unit is decoded at: its decoding time. A unit that has no time stamp of its own (only about every 16th has one in the PSP's own streams) is given -1, so it is the one
             * before it and one picture later. */
            if (ts_valid(v->au_avc.iDtsMSB, v->au_avc.iDts))
                v->au_ts = ((double)v->au_avc.iDts - PTS0) / 90000.0;
            else if (ts_valid(v->au_avc.iPtsMSB, v->au_avc.iPts))
                v->au_ts = au_sec(&v->au_avc) - 0.034;
            else if (v->have_ts)
                v->au_ts = v->last_ts + v->frame_dur;
            else
                v->au_ts = v->clock;
        }
        double pts = v->au_ts;
        if (!(v->stage & 16)) {
            v->stage |= 16;
            crumb4("first-unit", (int)(pts * 1000), (int)(v->clock * 1000), (int)v->au_avc.iAuSize, (int)v->au_avc.iPtsMSB);
        }
        if (pts > v->clock + 0.004)
            break; /* not yet */
        SceInt32 got = 0;
        void *buf = v->frame;
        if (!(v->stage & 2)) {
            v->stage |= 2;
            crumb("first-picture", v->au_avc.iAuSize, 0);
            crumbh("au", v->au_avc.iEsBuffer, v->au_avc.iAuSize, v->au_avc.iPts, v->au_avc.iDts);
            crumb_mem("es-head", v->au_avc.iEsBuffer);
            crumbh("frame", (unsigned)(uintptr_t)v->frame, FRAME_W, 0, 0);
        }
        v->decoded = 1;
        if (v->n_dec == 0)
            logs_step(LOG_VIDEO, "the games' library: the first picture is decoded");
        int r = sceMpegAvcDecode(&v->mpeg, &v->au_avc, FRAME_W, &buf, &got);
        v->have_au = 0;
        v->n_dec++;
        v->last_ts = v->au_ts;
        v->have_ts = 1;
        if (v->n_dec == 1)
            crumbh("first-decode", (unsigned)r, (unsigned)got, v->au_avc.iAuSize, (unsigned)(uintptr_t)buf);
        if (r != 0) {
            VLOG("avc decode %08x\n", r);
            v->err_total++;
            if ((unsigned)r == 0x80628002u)
                s_poisoned = 1; /* (the decoder's fatal error: a PSP gave nothing but 0x80020199 to every video after it) */
            if (v->dec_errors < 3)
                crumbh("decode-error", (unsigned)r, (unsigned)v->dec_errors, v->au_avc.iAuSize, 0);
            if (++v->dec_errors > 30)
                    video_mark_failed(v);
            continue;
        }
        v->dec_errors = 0;
        if (got == 1 && pts + 0.034 >= v->skip_until - 0.03) {
            shown = 1;
            v->have_pic = 1;
        }
    }
    if (v->duration > 0 && v->clock > v->duration + 0.5 && (v->src_eof & 1))
        v->ended = 1;
    return shown;
}
