/* psp5 – made by Mille Weber */
/* Spelets förhandsvisning som i XMB: står man på ett spel en stund spelas dess ICON1.PMF
 * (en kort video som ersätter ikonen) och SND0.AT3 (musik), om spelet har dem.
 * En styrtråd byter spel; video och ljud spelas i varsin tråd, så att ljudets blockerande
 * utmatning inte styr bildtakten. Videon avkodas med sceMpeg och ljudet med sceAtrac. */
#include <pspkernel.h>
#include <psputility.h>
#include <pspaudio.h>
#include <pspmpeg.h>
#include <pspatrac3.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "preview.h"
#include "art.h"
#include "music.h"
#include "system.h"

#define ALREADY_LOADED  0x80020139
#define ALREADY_LOADED2 0x80111102 /* samma sak, från sceUtilityLoadModule */
#define MAX_MEDIA  (2 * 1024 * 1024)
#define PACKET     2048
#define RB_PACKETS 64
#define BUF_W      512    /* sceMpegAvcDecode skriver rader om 512 bildpunkter */
#define MAX_VID_H  272
#define FRAMES     3      /* bilder i rotation: en visas, en kan läsas av GE, en avkodas */
#define VOLUME     0x5800 /* lite lägre än full volym */
#define MAX_SAMPLES 2048  /* ATRAC3plus ger 2048 per block, ATRAC3 1024 */
/* Avkodarna (sceMpegAvcDecode, sceAtracDecodeData) körs i trådens egen stack på en riktig
 * PSP (i PPSSPP gör de inte det) – ge dem gott om plats. */
#define STACK      0x10000

/* Felsökning på riktig PSP: varje steg skrivs till <data>/preview.log och filen stängs direkt,
 * så att raden finns kvar även om PSP:n fryser. shortcut: tas bort när förhandsvisningen
 * fungerar på hårdvaran. */
static void trace(const char *fmt, ...)
{
    char path[48], line[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > (int)sizeof(line) - 2)
        n = sizeof(line) - 2;
    line[n++] = '\n';
    snprintf(path, sizeof(path), "%s/preview.log", g_data_dir);
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, line, n);
        sceIoClose(fd);
    }
}

static SceUID s_thread = -1, s_lock = -1;
static volatile int s_quit, s_paused;
static volatile int s_gen;          /* ökar när önskat spel byts */
static int s_want = -1;             /* senast begärda index (huvudtråden) */
static Entry s_req;                 /* kopia av begärt spel, skyddad av s_lock */
static int s_req_index = -1;
static int s_mods[3];               /* moduler som preview.c själv laddade */
static int s_mpeg_ok;

/* aktuell förhandsvisning (styrtråden äger, spelartrådarna läser) */
static volatile int s_stop;
static SceUID s_audio = -1, s_video = -1;
static u8 *s_snd, *s_vid;
static u32 s_snd_size, s_vid_size, s_vid_pos, s_vid_end;
static Texture *s_tex[FRAMES];
static volatile int s_shown = -1;   /* bilden som visas, -1 = ingen än */
static volatile int s_frame_for = -1;

/* --- moduler --- */

static int load_module(int id, int slot)
{
    int r = sceUtilityLoadModule(id);
    s_mods[slot] = r >= 0 ? id : 0;
    return r >= 0 || (u32)r == ALREADY_LOADED || (u32)r == ALREADY_LOADED2;
}

static void unload_modules(void)
{
    if (s_mpeg_ok)
        sceMpegFinish();
    s_mpeg_ok = 0;
    for (int i = 2; i >= 0; i--) { /* omvänd ordning: AVCODEC sist */
        if (s_mods[i])
            sceUtilityUnloadModule(s_mods[i]);
        s_mods[i] = 0;
    }
}

/* --- ljud: SND0.AT3 --- */

static int audio_thread(SceSize args, void *argp)
{
    static short __attribute__((aligned(64))) out[2][MAX_SAMPLES * 2];
    trace("audio: start %u byte", s_snd_size);
    int id = sceAtracSetDataAndGetID(s_snd, s_snd_size);
    trace("audio: SetDataAndGetID = %08X", id);
    if (id < 0)
        return 0;
    sceAtracSetLoopNum(id, -1); /* filer med loop-punkt spelas om och om igen, som i XMB */
    int max = 0;
    int ch = sceAtracGetMaxSample(id, &max) >= 0 && max > 0 && max <= MAX_SAMPLES
             ? sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, max, PSP_AUDIO_FORMAT_STEREO) : -1;
    trace("audio: max %d, kanal %d", max, ch);
    int b = 0, vol = 0;
    int blocks = 0;
    while (ch >= 0 && !s_stop) {
        if (music_state() == MUSIC_PLAYING) { /* musikspelaren går före */
            sceKernelDelayThread(50 * 1000);
            continue;
        }
        int n = 0, end = 0, remain = 0;
        int r = sceAtracDecodeData(id, (u16 *)out[b], &n, &end, &remain);
        if (blocks < 3 || r < 0)
            trace("audio: block %d: Decode = %08X, %d samples, slut %d", blocks, r, n, end);
        blocks++;
        if (r < 0)
            break;
        if (n < max)
            memset(out[b] + n * 2, 0, (max - n) * 4);
        vol = vol + VOLUME / 20 < VOLUME ? vol + VOLUME / 20 : VOLUME; /* tonas in */
        sceAudioOutputBlocking(ch, vol, out[b]);
        b ^= 1;
        if (end)
            break;
    }
    if (ch >= 0) /* kanalen går inte att släppa medan sista bufferten spelas */
        for (int i = 0; i < 100 && sceAudioChRelease(ch) < 0; i++)
            sceKernelDelayThread(2000);
    trace("audio: slutar efter %d block", blocks);
    sceAtracReleaseAtracID(id);
    trace("audio: klar");
    return 0;
}

/* --- video: ICON1.PMF --- */

static SceInt32 ring_cb(ScePVoid data, SceInt32 packets, ScePVoid param)
{
    u32 left = s_vid_end > s_vid_pos ? (s_vid_end - s_vid_pos) / PACKET : 0;
    if ((u32)packets > left)
        packets = left;
    memcpy(data, s_vid + s_vid_pos, packets * PACKET);
    sceKernelDcacheWritebackRange(data, packets * PACKET); /* ringbufferten läses förbi cachen */
    s_vid_pos += packets * PACKET;
    return packets;
}

/* Spelar videon en gång. 0 = den gick inte att spela (försök inte igen). */
static int play_video_once(int w, int h)
{
    SceMpegRingbuffer rb;
    SceMpeg mpeg;
    int ok = 0, rb_size = sceMpegRingbufferQueryMemSize(RB_PACKETS), m_size = sceMpegQueryMemSize(0);
    void *rb_data = rb_size > 0 ? memalign(64, rb_size) : NULL, *m_data = m_size > 0 ? memalign(64, m_size) : NULL;
    trace("video: ringbuffer %d byte, mpeg %d byte", rb_size, m_size);
    if (!rb_data || !m_data || sceMpegRingbufferConstruct(&rb, RB_PACKETS, rb_data, rb_size, ring_cb, NULL) < 0) {
        trace("video: ringbuffer gick inte");
        free(rb_data);
        free(m_data);
        return 0;
    }
    int cr = sceMpegCreate(&mpeg, m_data, m_size, &rb, BUF_W, 0, 0);
    trace("video: Create = %08X", cr);
    if (cr >= 0) {
        SceMpegAvcMode mode = { -1, SCE_MPEG_AVC_FORMAT_8888 };
        SceInt32 offset = 0, size = 0;
        trace("video: DecodeMode = %08X", sceMpegAvcDecodeMode(&mpeg, &mode));
        int qo = sceMpegQueryStreamOffset(&mpeg, s_vid, &offset), qs = sceMpegQueryStreamSize(s_vid, &size);
        trace("video: Offset = %08X (%d), Size = %08X (%d)", qo, offset, qs, size);
        if (qo >= 0 && qs >= 0 && offset > 0 && (u32)offset < s_vid_size) {
            s_vid_pos = offset;
            s_vid_end = (u32)(offset + size) <= s_vid_size ? (u32)(offset + size) : s_vid_size;
            SceMpegStream *vs = sceMpegRegistStream(&mpeg, 0, 0);
            void *es = vs ? sceMpegMallocAvcEsBuf(&mpeg) : NULL;
            SceMpegAu au;
            int ia = es ? sceMpegInitAu(&mpeg, es, &au) : -1;
            trace("video: stream %p, es %p, InitAu = %08X", vs, es, ia);
            if (es && ia >= 0) {
                ok = 1;
                int next = 0, fails = 0, first = 1, frames = 0;
                u32 last_pts = 0;
                u64 due = sceKernelGetSystemTimeWide();
                while (!s_stop) {
                    int avail = sceMpegRingbufferAvailableSize(&rb);
                    if (avail > 0)
                        sceMpegRingbufferPut(&rb, avail, avail);
                    int ga = sceMpegGetAvcAu(&mpeg, vs, &au, NULL);
                    if (frames < 3)
                        trace("video: GetAvcAu = %08X (fyllt %d)", ga, avail);
                    if (ga < 0) {
                        if (s_vid_pos >= s_vid_end && ++fails > 2)
                            break; /* slut */
                        sceKernelDelayThread(5000);
                        continue;
                    }
                    fails = 0;
                    /* vänta in bildens tid (pts i 90 kHz) – utan rimlig pts: 29,97 bilder/s */
                    u32 step = au.iPts - last_pts;
                    if (!first)
                        due += step > 0 && step < 90 * 200 ? (u64)step * 1000 / 90 : 33367;
                    first = 0;
                    last_pts = au.iPts;
                    u64 now = sceKernelGetSystemTimeWide();
                    if (due > now + 500 * 1000 || due + 500 * 1000 < now)
                        due = now; /* för långt efter/före (t.ex. viloläge): börja om takten */
                    if (due > now)
                        sceKernelDelayThread(due - now);
                    void *dst = s_tex[next]->data;
                    SceInt32 got = 0;
                    if (frames < 3)
                        trace("video: Decode till %p ...", dst);
                    int dr = sceMpegAvcDecode(&mpeg, &au, BUF_W, &dst, &got);
                    if (frames < 3)
                        trace("video: ... Decode = %08X, bild %d", dr, got);
                    frames++;
                    if (dr >= 0 && got) {
                        /* Avkodaren kan lämna alfa 0 (genomskinligt) – gör bilden ogenomskinlig.
                         * Via den ocachade adressen: avkodaren skriver direkt i minnet, och
                         * cacheoperationer på delar av rader förstörde intilliggande minne. */
                        volatile u32 *px = (volatile u32 *)((u32)s_tex[next]->data | 0x40000000);
                        for (int y = 0; y < h; y++)
                            for (int x = 0; x < w; x++)
                                px[y * BUF_W + x] |= 0xFF000000;
                        s_shown = next;
                        next = (next + 1) % FRAMES;
                    }
                }
            }
            trace("video: avslutar");
            if (es)
                sceMpegFreeAvcEsBuf(&mpeg, es);
            /* Firmware tar en pekare till handtaget som alla andra sceMpeg-anrop – PSPSDK:s
             * header säger handtaget självt, och det fick en riktig PSP att frysa (pspautotests
             * gör så här). */
            if (vs)
                sceMpegUnRegistStream(&mpeg, vs);
        }
        sceMpegDelete(&mpeg);
    }
    sceMpegRingbufferDestruct(&rb);
    free(rb_data);
    free(m_data);
    trace("video: varv klart, ok %d", ok);
    return ok;
}

/* En bild som avkodaren skriver i: 64-byte-justerad och hela cacherader (avkodaren skriver
 * förbi cachen), tömd ur cachen en gång här – sedan rörs den bara via den ocachade adressen.
 * Visas som textur med w×h av raderna om BUF_W bildpunkter. Frigörs med tex_free. */
static Texture *frame_create(int w, int h)
{
    Texture *t = calloc(1, sizeof(Texture));
    u32 size = (BUF_W * (h + 1) * 4 + 63) & ~63; /* en extra rad för bilinjär filtrering */
    if (!t || !(t->data = memalign(64, size))) {
        free(t);
        return NULL;
    }
    memset(t->data, 0, size);
    sceKernelDcacheWritebackInvalidateRange(t->data, size);
    t->w = w; /* bara den del som videon fyller visas */
    t->h = h;
    t->tw = BUF_W;
    for (t->th = 1; t->th < h; t->th <<= 1)
        ;
    t->stride = BUF_W;
    t->psm = GU_PSM_8888;
    return t;
}

static int video_thread(SceSize args, void *argp)
{
    /* storleken står i PSMF-huvudet (i 16-tal); ICON1.PMF är normalt 144×80 */
    int w = s_vid[142] * 16, h = s_vid[143] * 16;
    trace("video: start %u byte, %dx%d", s_vid_size, w, h);
    if (w <= 0 || w > BUF_W || h <= 0 || h > MAX_VID_H)
        return 0;
    for (int i = 0; i < FRAMES; i++)
        if (!(s_tex[i] = frame_create(w, h)))
            return 0;
    trace("video: bildbuffertar %p %p %p", s_tex[0]->data, s_tex[1]->data, s_tex[2]->data);
    while (!s_stop && play_video_once(w, h)) /* om och om igen, som i XMB */
        ;
    return 0;
}

/* --- styrning --- */

static void stop_players(void)
{
    s_stop = 1;
    SceUID *th[2] = { &s_audio, &s_video };
    for (int i = 0; i < 2; i++) {
        if (*th[i] >= 0) {
            sceKernelWaitThreadEnd(*th[i], NULL);
            sceKernelDeleteThread(*th[i]);
            *th[i] = -1;
        }
    }
    s_frame_for = -1;
    s_shown = -1;
    if (s_tex[0])
        sceKernelDelayThread(50 * 1000); /* GE kan fortfarande läsa bilden som ritades sist */
    for (int i = 0; i < FRAMES; i++) {
        tex_free(s_tex[i]);
        s_tex[i] = NULL;
    }
    free(s_snd);
    free(s_vid);
    s_snd = s_vid = NULL;
    s_stop = 0;
}

/* Läser en fil ur spelet till 64-byte-justerat minne, utskrivet ur cachen: avkodarna
 * (mediaprocessorn) läser direkt ur minnet, förbi processorns cache. */
static u8 *read_media(const Entry *e, u32 offset, u32 size, const char *umd_name)
{
    u8 *raw = art_read_part(e, offset, size, umd_name, MAX_MEDIA);
    u8 *buf = raw ? memalign(64, (size + 63) & ~63) : NULL;
    if (buf) {
        memcpy(buf, raw, size);
        sceKernelDcacheWritebackRange(buf, size);
    }
    free(raw);
    return buf;
}

static void start_players(const Entry *e, int index)
{
    if (s_mpeg_ok)
        s_vid = read_media(e, e->vid_offset, e->vid_size, "ICON1.PMF");
    s_vid_size = s_vid ? e->vid_size : 0;
    if (s_vid && (s_vid_size < PACKET || memcmp(s_vid, "PSMF", 4))) {
        free(s_vid);
        s_vid = NULL;
    }
    s_snd = read_media(e, e->snd_offset, e->snd_size, "SND0.AT3");
    s_snd_size = s_snd ? e->snd_size : 0;
    s_frame_for = index;
    if (s_vid) {
        s_video = sceKernelCreateThread("psp5_pvideo", video_thread, 0x28, STACK, PSP_THREAD_ATTR_USER, NULL);
        if (s_video >= 0 && sceKernelStartThread(s_video, 0, NULL) < 0) {
            sceKernelDeleteThread(s_video);
            s_video = -1;
        }
    }
    if (s_snd) { /* över bildladdaren, under gränssnittsljuden */
        s_audio = sceKernelCreateThread("psp5_paudio", audio_thread, 0x16, STACK, PSP_THREAD_ATTR_USER, NULL);
        if (s_audio >= 0 && sceKernelStartThread(s_audio, 0, NULL) < 0) {
            sceKernelDeleteThread(s_audio);
            s_audio = -1;
        }
    }
}

static int control_thread(SceSize args, void *argp)
{
    /* AVCODEC först; MPEGBASE behövs bara för videon */
    char path[48];
    snprintf(path, sizeof(path), "%s/preview.log", g_data_dir);
    sceIoRemove(path);
    trace("förhandsvisning startar (bygge " __DATE__ " " __TIME__ ")");
    int ok_av = load_module(PSP_MODULE_AV_AVCODEC, 0), ok_at = ok_av && load_module(PSP_MODULE_AV_ATRAC3PLUS, 1);
    trace("moduler: AVCODEC %d, ATRAC3PLUS %d", ok_av, ok_at);
    if (!ok_at)
        return 0;
    if (load_module(PSP_MODULE_AV_MPEGBASE, 2)) {
        int r = sceMpegInit();
        trace("moduler: MPEGBASE ok, sceMpegInit = %08X", r);
        s_mpeg_ok = r >= 0;
    } else {
        trace("moduler: MPEGBASE gick inte");
    }
    int gen = -1;
    while (!s_quit) {
        if (gen != s_gen) {
            Entry e;
            sceKernelWaitSema(s_lock, 1, NULL);
            gen = s_gen;
            int index = s_req_index;
            if (index >= 0)
                e = s_req;
            sceKernelSignalSema(s_lock, 1);
            trace("byte till %d: stoppar", index);
            stop_players();
            trace("stoppat");
            if (index >= 0) {
                trace("startar \"%s\" (video %u, ljud %u byte)", e.title, e.vid_size, e.snd_size);
                start_players(&e, index);
                trace("startat: video %d, ljud %d", s_video >= 0, s_audio >= 0);
            }
        }
        sceKernelDelayThread(20 * 1000);
    }
    stop_players();
    return 0;
}

void preview_want(const Entry *e, int list_index)
{
    if (s_paused || !e)
        list_index = -1;
    if (list_index == s_want)
        return;
    if (list_index >= 0 && !e->vid_size && !e->snd_size)
        list_index = -1; /* spelet har ingen förhandsvisning */
    if (list_index == s_want)
        return;
    if (s_thread < 0) {
        if (list_index < 0)
            return;
        s_quit = 0;
        s_lock = sceKernelCreateSema("psp5_preview", 0, 1, 1, NULL);
        s_thread = s_lock >= 0 ? sceKernelCreateThread("psp5_preview", control_thread, 0x30, STACK,
                                                        PSP_THREAD_ATTR_USER, NULL) : -1;
        if (s_thread >= 0 && sceKernelStartThread(s_thread, 0, NULL) < 0) {
            sceKernelDeleteThread(s_thread);
            s_thread = -1;
        }
        if (s_thread < 0) {
            if (s_lock >= 0)
                sceKernelDeleteSema(s_lock);
            s_lock = -1;
            return;
        }
    }
    s_want = list_index;
    sceKernelWaitSema(s_lock, 1, NULL);
    if (list_index >= 0)
        s_req = *e;
    s_req_index = list_index;
    s_gen++;
    sceKernelSignalSema(s_lock, 1);
}

void preview_pause(int paused)
{
    s_paused = paused;
    if (paused)
        preview_want(NULL, -1);
}

Texture *preview_frame(int list_index)
{
    int shown = s_shown;
    if (list_index < 0 || list_index != s_frame_for || shown < 0)
        return NULL;
    return s_tex[shown];
}

void preview_stop(void)
{
    if (s_thread >= 0) {
        s_quit = 1;
        sceKernelWaitThreadEnd(s_thread, NULL);
        sceKernelDeleteThread(s_thread);
        s_thread = -1;
        unload_modules();
    }
    if (s_lock >= 0) {
        sceKernelDeleteSema(s_lock);
        s_lock = -1;
    }
    s_want = s_req_index = -1;
}
