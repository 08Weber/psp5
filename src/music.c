/* psp5 – made by Mille Weber */
/* MP3-spelaren: skannar ms0:/MUSIC (läser ID3-taggar) i bakgrunden och spelar upp
 * låtarna i en egen tråd med PSP:ns inbyggda avkodare (sceMp3) via SRC-ljudkanalen,
 * som klarar 44,1 kHz, 48 kHz m.fl. Gränssnittet styr tråden med en liten kommandokö,
 * så att inget behöver låsas. Buffertarna är små (~50 KB) så att det räcker även
 * på PSP-1000. */
#include <pspkernel.h>
#include <pspaudio.h>
#include <psputility.h>
#include <pspmp3.h>
#include <pspsysmem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "music.h"

#define MAX_TRACKS   1000
#define MAX_DEPTH    3
#define TAG_READ     8192   /* så mycket av ID3v2-taggen som läses vid skanning */
#define MP3_BUF      (32 * 1024)
#define PCM_BUF      (1152 * 2 * 2 * 2) /* avkodaren dubbelbuffrar själv i den här */
#define MAX_SAMPLES  1152
#define MP3_END      0x80671402 /* sceMp3Decode: strömmen är slut */
#define ALREADY_LOADED  0x80020139
#define ALREADY_LOADED2 0x80111102 /* samma sak, från sceUtilityLoadModule */
#define MUSIC_ROOT   "ms0:/MUSIC"

/* --- låtlistan --- */

typedef struct {
    Track t;
    int trackno; /* från TRCK, för ordningen inom ett album */
} Item;

static Item *volatile s_items;  /* publicerad lista – byts bara när tråden inte spelar */
static volatile int s_count;
static volatile int s_gen;      /* ökas när listan byts, så att UI:t vet när dess cache är gammal */
static volatile int s_scanning; /* 1 medan bakgrundsskanningen pågår */
static volatile int s_cjk;      /* 1 om någon titel/artist behöver det japanska typsnittet */
/* listan som byggs under skanningen (bara spelartråden rör den) */
static Item *s_scan_items;
static int s_scan_count, s_scan_cap;

/* --- kommandon från gränssnittet (en skrivare, en läsare) --- */

enum { CMD_PLAY, CMD_PAUSE, CMD_STOP, CMD_NEXT, CMD_PREV };
#define QUEUE 16
static volatile int s_q_cmd[QUEUE], s_q_arg[QUEUE];
static volatile int s_q_head, s_q_tail; /* head skrivs bara av UI, tail bara av tråden */

/* --- tillstånd som tråden skriver och UI läser --- */

static volatile int s_state = MUSIC_STOPPED;
static volatile int s_current = -1;
static volatile float s_elapsed, s_duration;

/* --- tråd och avkodare --- */

static SceUID s_thread = -1;
static volatile int s_quit;
static int s_ready;                      /* moduler + resurs laddade */
static int s_loaded_avcodec, s_loaded_mp3;

static unsigned char __attribute__((aligned(64))) s_mp3buf[MP3_BUF];
static unsigned char __attribute__((aligned(64))) s_pcmbuf[PCM_BUF];
static short __attribute__((aligned(64))) s_out[2][MAX_SAMPLES * 2];
static int s_out_idx;

static SceUID s_fd = -1;
static int s_handle = -1;
static int s_eof;
static int s_rate, s_channels;
static int s_spf;                      /* prover per bildruta (1152/576), 0 = okänt */
static int s_src_rate, s_src_samples;  /* reserverad SRC-kanal, 0 = ingen */
static int s_open_index = -1;          /* låten som avkodaren har öppen */

/* ---------------------------------------------------------------------- */
/* ID3 */

static u32 syncsafe(const unsigned char *p)
{
    return ((u32)(p[0] & 0x7F) << 21) | ((u32)(p[1] & 0x7F) << 14) | ((u32)(p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

static u32 be32(const unsigned char *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

/* Skriver ett tecken som UTF-8; returnerar 0 om det inte fick plats */
static int put_utf8(char *out, int *n, int size, u32 c)
{
    char tmp[4];
    int len;
    if (c < 0x80) { tmp[0] = c; len = 1; }
    else if (c < 0x800) { tmp[0] = 0xC0 | (c >> 6); tmp[1] = 0x80 | (c & 0x3F); len = 2; }
    else if (c < 0x10000) { tmp[0] = 0xE0 | (c >> 12); tmp[1] = 0x80 | ((c >> 6) & 0x3F); tmp[2] = 0x80 | (c & 0x3F); len = 3; }
    else { tmp[0] = 0xF0 | (c >> 18); tmp[1] = 0x80 | ((c >> 12) & 0x3F); tmp[2] = 0x80 | ((c >> 6) & 0x3F); tmp[3] = 0x80 | (c & 0x3F); len = 4; }
    if (*n + len >= size)
        return 0;
    memcpy(out + *n, tmp, len);
    *n += len;
    return 1;
}

static void trim(char *s)
{
    int n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
    int lead = 0;
    while (s[lead] == ' ')
        lead++;
    if (lead)
        memmove(s, s + lead, n - lead + 1);
}

/* Textram (kodningsbyte + text) → UTF-8 */
static void id3_text(const unsigned char *p, int len, char *out, int size)
{
    int n = 0;
    out[0] = 0;
    if (len < 1)
        return;
    int enc = p[0];
    p++;
    len--;
    if (enc == 1 || enc == 2) { /* UTF-16 med BOM / UTF-16BE */
        int big = 1;
        if (enc == 1 && len >= 2) {
            if (p[0] == 0xFF && p[1] == 0xFE) { big = 0; p += 2; len -= 2; }
            else if (p[0] == 0xFE && p[1] == 0xFF) { p += 2; len -= 2; }
        }
        for (int i = 0; i + 1 < len; i += 2) {
            u32 c = big ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]);
            if (!c)
                break;
            if (c >= 0xD800 && c < 0xDC00 && i + 3 < len) { /* surrogatpar */
                u32 lo = big ? (p[i + 2] << 8 | p[i + 3]) : (p[i + 3] << 8 | p[i + 2]);
                if (lo >= 0xDC00 && lo < 0xE000) {
                    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                    i += 2;
                }
            }
            if (!put_utf8(out, &n, size, c))
                break;
        }
    } else if (enc == 3) { /* UTF-8 */
        int i = 0;
        while (i < len && p[i] && n + 1 < size)
            out[n++] = p[i++];
        /* fick inte allt plats: skär inte mitt i ett tecken */
        if (i < len && p[i])
            while (n > 0 && (p[n] & 0xC0) == 0x80)
                n--;
    } else { /* Latin-1 */
        for (int i = 0; i < len && p[i]; i++)
            if (!put_utf8(out, &n, size, p[i]))
                break;
    }
    out[n] = 0;
    trim(out);
}

/* Storleken på ID3v2-taggen i början av filen (0 om ingen) */
static u32 id3v2_size(SceUID fd)
{
    unsigned char h[10];
    sceIoLseek32(fd, 0, PSP_SEEK_SET);
    if (sceIoRead(fd, h, 10) != 10 || memcmp(h, "ID3", 3) || h[3] == 0xFF)
        return 0;
    return 10 + syncsafe(h + 6) + ((h[5] & 0x10) ? 10 : 0);
}

/* Läser titel, artist, album och spårnummer ur ID3v2 (v2.2–v2.4) och ID3v1. */
static void read_tags(SceUID fd, u32 file_size, Item *it, unsigned char *buf)
{
    unsigned char h[10];
    sceIoLseek32(fd, 0, PSP_SEEK_SET);
    if (sceIoRead(fd, h, 10) == 10 && !memcmp(h, "ID3", 3) && h[3] >= 2 && h[3] <= 4) {
        int ver = h[3], flags = h[5];
        u32 size = syncsafe(h + 6);
        int len = size < TAG_READ ? (int)size : TAG_READ;
        len = sceIoRead(fd, buf, len);
        if (len > 0) {
            /* v2.2/v2.3: hela taggen kan vara "unsynchronised" (FF 00 → FF) */
            if ((flags & 0x80) && ver < 4) {
                int w = 0;
                for (int r = 0; r < len; r++) {
                    buf[w++] = buf[r];
                    if (buf[r] == 0xFF && r + 1 < len && buf[r + 1] == 0)
                        r++;
                }
                len = w;
            }
            int pos = 0;
            if ((flags & 0x40) && ver >= 3 && len >= 4) { /* utökad header */
                u32 ext = ver == 3 ? be32(buf) : syncsafe(buf);
                /* trasig storlek: hoppa över ramarna i stället för att läsa utanför */
                pos = ext < (u32)len - 4 ? (int)ext + (ver == 3 ? 4 : 0) : len;
            }
            int idlen = ver == 2 ? 3 : 4, hdr = ver == 2 ? 6 : 10;
            while (pos + hdr <= len && buf[pos]) {
                const unsigned char *f = buf + pos;
                u32 fsize = ver == 2 ? ((u32)f[3] << 16 | f[4] << 8 | f[5]) : ver == 4 ? syncsafe(f + 4) : be32(f + 4);
                if (fsize == 0 || fsize > (u32)(len - pos - hdr)) /* även "negativa" storlekar */
                    break;
                const unsigned char *d = f + hdr;
                if (!memcmp(f, ver == 2 ? "TT2" : "TIT2", idlen))
                    id3_text(d, fsize, it->t.title, sizeof(it->t.title));
                else if (!memcmp(f, ver == 2 ? "TP1" : "TPE1", idlen))
                    id3_text(d, fsize, it->t.artist, sizeof(it->t.artist));
                else if (!memcmp(f, ver == 2 ? "TAL" : "TALB", idlen))
                    id3_text(d, fsize, it->t.album, sizeof(it->t.album));
                else if (!memcmp(f, ver == 2 ? "TRK" : "TRCK", idlen)) {
                    char num[16];
                    id3_text(d, fsize, num, sizeof(num));
                    it->trackno = atoi(num);
                }
                pos += hdr + fsize;
            }
        }
    }
    /* ID3v1 sist i filen, om något fortfarande saknas */
    if ((!it->t.title[0] || !it->t.artist[0] || !it->t.album[0]) && file_size > 128 + 10) {
        unsigned char v1[128];
        sceIoLseek32(fd, file_size - 128, PSP_SEEK_SET);
        if (sceIoRead(fd, v1, 128) == 128 && !memcmp(v1, "TAG", 3)) {
            char *fields[3] = { it->t.title, it->t.artist, it->t.album };
            int sizes[3] = { sizeof(it->t.title), sizeof(it->t.artist), sizeof(it->t.album) };
            for (int k = 0; k < 3; k++) {
                if (fields[k][0])
                    continue;
                unsigned char tmp[32];
                tmp[0] = 0; /* Latin-1 */
                memcpy(tmp + 1, v1 + 3 + k * 30, 30);
                id3_text(tmp, 31, fields[k], sizes[k]);
            }
            if (!it->trackno && v1[125] == 0 && v1[126])
                it->trackno = v1[126];
        }
    }
}

/* ---------------------------------------------------------------------- */
/* skanning */

static int has_mp3_ext(const char *name)
{
    int n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".mp3");
}

static void add_track(const char *path, const char *name, const char *folder, unsigned char *buf)
{
    if (s_scan_count >= MAX_TRACKS)
        return;
    if (s_scan_count >= s_scan_cap) {
        int ncap = s_scan_cap ? s_scan_cap * 2 : 64;
        Item *n = realloc(s_scan_items, ncap * sizeof(Item));
        if (!n)
            return;
        s_scan_items = n;
        s_scan_cap = ncap;
    }
    Item *it = &s_scan_items[s_scan_count];
    memset(it, 0, sizeof(*it));
    snprintf(it->t.path, sizeof(it->t.path), "%s", path);

    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    if (fd >= 0) {
        u32 size = sceIoLseek32(fd, 0, PSP_SEEK_END);
        read_tags(fd, size, it, buf);
        sceIoClose(fd);
    }
    if (!it->t.title[0]) { /* filnamnet utan .mp3 */
        snprintf(it->t.title, sizeof(it->t.title), "%.*s", (int)strlen(name) - 4, name);
    }
    if (!it->t.album[0] && folder)
        snprintf(it->t.album, sizeof(it->t.album), "%s", folder);
    s_scan_count++;
}

static void scan_dir(const char *dir, const char *folder, int depth, unsigned char *buf)
{
    SceUID d = sceIoDopen(dir);
    if (d < 0)
        return;
    SceIoDirent ent;
    char path[256];
    memset(&ent, 0, sizeof(ent));
    while (!s_quit && sceIoDread(d, &ent) > 0 && s_scan_count < MAX_TRACKS) {
        if (ent.d_name[0] != '.') {
            if (snprintf(path, sizeof(path), "%s/%s", dir, ent.d_name) < (int)sizeof(path)) {
                if (FIO_S_ISDIR(ent.d_stat.st_mode)) {
                    if (depth < MAX_DEPTH) {
                        char sub[64];
                        snprintf(sub, sizeof(sub), "%s", ent.d_name);
                        scan_dir(path, sub, depth + 1, buf);
                    }
                } else if (has_mp3_ext(ent.d_name)) {
                    add_track(path, ent.d_name, folder, buf);
                }
            }
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(d);
}

/* skiftlägesokänslig jämförelse där tomma fält (okänd artist/album) hamnar sist */
static int cmp_text(const char *a, const char *b)
{
    if (!a[0] || !b[0])
        return !a[0] - !b[0];
    for (;; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb || !ca)
            return ca - cb;
    }
}

static int cmp_items(const void *a, const void *b)
{
    const Item *x = a, *y = b;
    int c = cmp_text(x->t.artist, y->t.artist);
    if (!c) c = cmp_text(x->t.album, y->t.album);
    if (!c && x->trackno != y->trackno) c = x->trackno - y->trackno;
    if (!c) c = cmp_text(x->t.title, y->t.title);
    if (!c) c = strcmp(x->t.path, y->t.path);
    return c;
}

/* kyrilliska, grekiska, kana, kanji … (UTF-8-ledbyte ≥ 0xCE) finns inte i det latinska typsnittet */
static int needs_cjk(const char *t)
{
    for (; *t; t++)
        if ((unsigned char)*t >= 0xCE)
            return 1;
    return 0;
}

/* Körs i spelartråden innan något spelas: bygger listan och publicerar den färdig. */
static void scan_library(void)
{
    s_scan_items = NULL;
    s_scan_count = s_scan_cap = 0;
    unsigned char *buf = malloc(TAG_READ);
    if (buf) {
        scan_dir(MUSIC_ROOT, NULL, 0, buf);
        free(buf);
    }
    if (s_quit) { /* avbruten av music_term */
        free(s_scan_items);
        s_scan_items = NULL;
        s_scan_count = 0;
        return;
    }
    if (s_scan_count > 1)
        qsort(s_scan_items, s_scan_count, sizeof(Item), cmp_items);
    int cjk = 0;
    for (int i = 0; i < s_scan_count && !cjk; i++)
        cjk = needs_cjk(s_scan_items[i].t.title) || needs_cjk(s_scan_items[i].t.artist);
    s_cjk = cjk;
    s_items = s_scan_items; /* pekaren före antalet – UI:t läser antalet först */
    s_count = s_scan_count;
    s_gen++;
    s_scan_items = NULL;
}

int music_count(void)
{
    return s_count;
}

const Track *music_track(int index)
{
    return index >= 0 && index < s_count ? &s_items[index].t : NULL;
}

/* ---------------------------------------------------------------------- */
/* uppspelning (allt här körs i spelartråden) */

static void src_release(void)
{
    if (!s_src_rate)
        return;
    /* kanalen går inte att släppa medan sista bufferten spelas – vänta lite */
    for (int i = 0; i < 100 && sceAudioSRCChRelease() < 0; i++)
        sceKernelDelayThread(2000);
    s_src_rate = s_src_samples = 0;
}

static int src_reserve(int samples, int rate)
{
    if (s_src_rate == rate && s_src_samples == samples)
        return 1;
    src_release();
    if (sceAudioSRCChReserve(samples, rate, 2) < 0)
        return 0;
    s_src_rate = rate;
    s_src_samples = samples;
    return 1;
}

static void close_track(void)
{
    if (s_handle >= 0) {
        sceMp3ReleaseMp3Handle(s_handle);
        s_handle = -1;
    }
    if (s_fd >= 0) {
        sceIoClose(s_fd);
        s_fd = -1;
    }
    s_open_index = -1;
}

/* Fyller på avkodarens buffert från filen. <0 = läsfel. */
static int feed(void)
{
    unsigned char *dst;
    SceInt32 towrite, srcpos;
    if (s_eof)
        return 0;
    if (sceMp3GetInfoToAddStreamData(s_handle, &dst, &towrite, &srcpos) < 0)
        return -1;
    if (towrite <= 0)
        return 0;
    int got = -1;
    if (sceIoLseek32(s_fd, srcpos, PSP_SEEK_SET) >= 0)
        got = sceIoRead(s_fd, dst, towrite);
    if (got < 0 && s_open_index >= 0) {
        /* filen stängs av viloläge – öppna den igen och försök en gång till */
        sceIoClose(s_fd);
        s_fd = sceIoOpen(s_items[s_open_index].t.path, PSP_O_RDONLY, 0777);
        if (s_fd >= 0 && sceIoLseek32(s_fd, srcpos, PSP_SEEK_SET) >= 0)
            got = sceIoRead(s_fd, dst, towrite);
    }
    if (got < 0)
        return -1;
    if (got == 0) {
        s_eof = 1;
        return 0;
    }
    return sceMp3NotifyAddStreamData(s_handle, got) < 0 ? -1 : 0;
}

/* Läser första bildrutans huvud: prover per bildruta till s_spf, och längden från
 * Xing/Info- eller VBRI-huvudet (VBR-filer). Returnerar 0 om längden inte står där. */
static float parse_first_frame(u32 start)
{
    static const int rates[4][3] = { { 11025, 12000, 8000 }, { 0, 0, 0 }, { 22050, 24000, 16000 }, { 44100, 48000, 32000 } };
    unsigned char b[192];
    sceIoLseek32(s_fd, start, PSP_SEEK_SET);
    if (sceIoRead(s_fd, b, sizeof(b)) != sizeof(b))
        return 0;
    u32 h = be32(b);
    if ((h & 0xFFE00000) != 0xFFE00000)
        return 0;
    int ver = (h >> 19) & 3, layer = (h >> 17) & 3, ri = (h >> 10) & 3, mono = ((h >> 6) & 3) == 3;
    if (ver == 1 || layer != 1 || ri == 3)
        return 0;
    int rate = rates[ver][ri], spf = ver == 3 ? 1152 : 576;
    s_spf = spf;
    int off = ver == 3 ? (mono ? 21 : 36) : (mono ? 13 : 21);
    u32 frames = 0;
    if (!memcmp(b + off, "Xing", 4) || !memcmp(b + off, "Info", 4)) {
        if (be32(b + off + 4) & 1)
            frames = be32(b + off + 8);
    } else if (!memcmp(b + 36, "VBRI", 4)) {
        frames = be32(b + 36 + 14);
    }
    return frames && rate ? (float)frames * spf / rate : 0;
}

static int open_track(int index)
{
    close_track();
    if (index < 0 || index >= s_count)
        return -1;
    s_fd = sceIoOpen(s_items[index].t.path, PSP_O_RDONLY, 0777);
    if (s_fd < 0)
        return -1;
    u32 size = sceIoLseek32(s_fd, 0, PSP_SEEK_END);
    u32 start = id3v2_size(s_fd); /* strömmen börjar efter taggen */
    if (start >= size)
        start = 0;

    SceMp3InitArg args;
    memset(&args, 0, sizeof(args));
    args.mp3StreamStart = start;
    args.mp3StreamEnd = size;
    args.mp3Buf = s_mp3buf;
    args.mp3BufSize = sizeof(s_mp3buf);
    args.pcmBuf = s_pcmbuf;
    args.pcmBufSize = sizeof(s_pcmbuf);
    s_handle = sceMp3ReserveMp3Handle(&args);
    if (s_handle < 0) {
        close_track();
        return -1;
    }
    s_eof = 0;
    if (feed() < 0 || sceMp3Init(s_handle) < 0) {
        close_track();
        return -1;
    }
    sceMp3SetLoopNum(s_handle, 0);
    s_rate = sceMp3GetSamplingRate(s_handle);
    s_channels = sceMp3GetMp3ChannelNum(s_handle);
    if (s_rate <= 0 || s_channels < 1 || s_channels > 2) {
        close_track();
        return -1;
    }
    s_spf = 0;
    float dur = parse_first_frame(start);
    if (dur <= 0) {
        int kbps = sceMp3GetBitRate(s_handle);
        dur = kbps > 0 ? (float)(size - start) * 8.0f / (kbps * 1000.0f) : 0;
    }
    s_duration = dur;
    s_elapsed = 0;
    s_open_index = index;
    return 0;
}

/* Avkodar och spelar en bildruta. 1 = fortsätt, 0 = låten slut, -1 = fel */
static int play_frame(void)
{
    if (sceMp3CheckStreamDataNeeded(s_handle) > 0 && feed() < 0)
        return -1;
    SceShort16 *pcm;
    int bytes = sceMp3Decode(s_handle, &pcm);
    if (bytes == 0 || (u32)bytes == MP3_END)
        return 0;
    if (bytes < 0)
        return -1;
    /* avkodaren kan lämna monofiler som stereo – avgör från bildrutans storlek */
    int out_ch = s_channels;
    if (s_spf && bytes == s_spf * 4)
        out_ch = 2;
    else if (s_spf && bytes == s_spf * 2)
        out_ch = 1;
    else if (bytes / (2 * out_ch) > MAX_SAMPLES)
        out_ch = 2;
    int samples = bytes / (2 * out_ch);
    if (samples <= 0 || samples > MAX_SAMPLES)
        return -1;
    /* kanalen reserveras för en hel bildruta: 1152 prover (MPEG-1) eller 576 (MPEG-2/2.5);
     * en kortare bildruta fylls ut i stället för att kanalen byts */
    int want = samples > 576 ? 1152 : 576;
    if (s_src_rate == s_rate && s_src_samples >= samples)
        want = s_src_samples;
    if (!src_reserve(want, s_rate))
        return -1;
    void *out = pcm;
    if (out_ch == 1 || samples != s_src_samples) {
        /* mono → stereo, och fyll ut korta bildrutor med tystnad */
        short *o = s_out[s_out_idx];
        s_out_idx ^= 1;
        for (int i = 0; i < samples; i++) {
            if (out_ch == 1) {
                o[i * 2] = o[i * 2 + 1] = pcm[i];
            } else {
                o[i * 2] = pcm[i * 2];
                o[i * 2 + 1] = pcm[i * 2 + 1];
            }
        }
        memset(o + samples * 2, 0, (s_src_samples - samples) * 4);
        out = o;
    }
    sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, out);
    int sum = sceMp3GetSumDecodedSample(s_handle);
    if (sum >= 0 && s_rate > 0)
        s_elapsed = (float)sum / s_rate;
    return 1;
}

/* Startar låten index (och hoppar över trasiga filer framåt). */
static void start_at(int index)
{
    for (int tries = 0; tries < s_count && index >= 0 && index < s_count; tries++) {
        if (s_quit || (tries > 0 && s_q_head != s_q_tail))
            break; /* psp5 stänger, eller användaren valde något annat */
        if (open_track(index) == 0) {
            s_current = index;
            s_state = MUSIC_PLAYING;
            return;
        }
        index++;
    }
    close_track();
    src_release();
    s_current = -1;
    s_state = MUSIC_STOPPED;
    s_elapsed = s_duration = 0;
}

static void stop_playback(void)
{
    close_track();
    src_release();
    s_current = -1;
    s_state = MUSIC_STOPPED;
    s_elapsed = s_duration = 0;
}

static void handle_command(int cmd, int arg)
{
    switch (cmd) {
    case CMD_PLAY:
        start_at(arg);
        break;
    case CMD_PAUSE:
        if (s_state == MUSIC_PLAYING)
            s_state = MUSIC_PAUSED;
        else if (s_state == MUSIC_PAUSED)
            s_state = MUSIC_PLAYING;
        break;
    case CMD_STOP:
        stop_playback();
        break;
    case CMD_NEXT:
        if (s_current >= 0 && s_current + 1 < s_count)
            start_at(s_current + 1);
        else if (s_current >= 0)
            stop_playback();
        break;
    case CMD_PREV:
        if (s_current < 0)
            break;
        start_at(s_elapsed > 3.0f || s_current == 0 ? s_current : s_current - 1);
        break;
    }
}

static int player_thread(SceSize args, void *argp)
{
    /* skanningen först, med lägre prioritet än gränssnittet, så att inget hackar */
    sceKernelChangeThreadPriority(0, 0x30);
    scan_library();
    s_scanning = 0;
    sceKernelChangeThreadPriority(0, 0x14);
    s_q_tail = s_q_head; /* kommandon från innan listan fanns gäller inte */
    int failures = 0;
    while (!s_quit) {
        while (s_q_tail != s_q_head) {
            int t = s_q_tail;
            handle_command(s_q_cmd[t], s_q_arg[t]);
            s_q_tail = (t + 1) % QUEUE;
            failures = 0;
        }
        if (s_state != MUSIC_PLAYING || s_handle < 0) {
            sceKernelDelayThread(10000);
            continue;
        }
        int r = play_frame();
        if (r > 0) {
            failures = 0;
            continue;
        }
        /* slut eller fel: nästa låt (efter sista: stopp) */
        if (r < 0 && ++failures >= (s_count < 8 ? s_count : 8)) {
            stop_playback();
            continue;
        }
        if (s_current + 1 < s_count)
            start_at(s_current + 1);
        else
            stop_playback();
    }
    close_track();
    src_release();
    return 0;
}

static void push(int cmd, int arg)
{
    if (s_thread < 0)
        return;
    int h = s_q_head, n = (h + 1) % QUEUE;
    if (n == s_q_tail)
        return; /* kön full – kommandot släpps */
    s_q_cmd[h] = cmd;
    s_q_arg[h] = arg;
    s_q_head = n;
}

/* ---------------------------------------------------------------------- */
/* livscykel och styrning */

void music_init(void)
{
    if (s_thread >= 0)
        return;
    /* utan angiven SDK-version godtar sceMp3 bara 44,1 kHz (48 kHz m.fl. avvisas) */
    if (sceKernelGetCompiledSdkVersion() == 0)
        sceKernelSetCompiledSdkVersion(0x06060010);
    int r = sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
    s_loaded_avcodec = r >= 0;
    if (r < 0 && (u32)r != ALREADY_LOADED && (u32)r != ALREADY_LOADED2)
        return;
    r = sceUtilityLoadModule(PSP_MODULE_AV_MP3);
    s_loaded_mp3 = r >= 0;
    if (r < 0 && (u32)r != ALREADY_LOADED && (u32)r != ALREADY_LOADED2) {
        if (s_loaded_avcodec)
            sceUtilityUnloadModule(PSP_MODULE_AV_AVCODEC);
        s_loaded_avcodec = 0;
        return;
    }
    if (sceMp3InitResource() < 0) {
        if (s_loaded_mp3)
            sceUtilityUnloadModule(PSP_MODULE_AV_MP3);
        if (s_loaded_avcodec)
            sceUtilityUnloadModule(PSP_MODULE_AV_AVCODEC);
        s_loaded_mp3 = s_loaded_avcodec = 0;
        return;
    }
    s_ready = 1;
    s_quit = 0;
    s_q_head = s_q_tail = 0;
    s_state = MUSIC_STOPPED;
    s_current = -1;
    s_elapsed = s_duration = 0;
    s_scanning = 1;
    s_thread = sceKernelCreateThread("psp5_music", player_thread, 0x14, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (s_thread >= 0 && sceKernelStartThread(s_thread, 0, NULL) < 0) {
        sceKernelDeleteThread(s_thread);
        s_thread = -1;
    }
    if (s_thread < 0)
        s_scanning = 0;
}

void music_term(void)
{
    if (s_thread >= 0) {
        s_quit = 1;
        sceKernelWaitThreadEnd(s_thread, NULL);
        sceKernelDeleteThread(s_thread);
        s_thread = -1;
    }
    close_track();
    src_release();
    if (s_ready) {
        sceMp3TermResource();
        s_ready = 0;
    }
    if (s_loaded_mp3)
        sceUtilityUnloadModule(PSP_MODULE_AV_MP3);
    if (s_loaded_avcodec)
        sceUtilityUnloadModule(PSP_MODULE_AV_AVCODEC);
    s_loaded_mp3 = s_loaded_avcodec = 0;
    s_count = 0; /* antalet före pekaren – UI:t läser antalet först */
    free(s_items);
    s_items = NULL;
    free(s_scan_items);
    s_scan_items = NULL;
    s_scanning = 0;
    s_cjk = 0;
    s_gen++;
    s_state = MUSIC_STOPPED;
    s_current = -1;
    s_elapsed = s_duration = 0;
}

void music_play(int index)
{
    if (index >= 0 && index < s_count)
        push(CMD_PLAY, index);
}

void music_toggle_pause(void) { push(CMD_PAUSE, 0); }
void music_stop(void)         { push(CMD_STOP, 0); }
void music_next(void)         { push(CMD_NEXT, 0); }
void music_prev(void)         { push(CMD_PREV, 0); }

int music_scanning(void)   { return s_scanning; }
int music_generation(void) { return s_gen; }
int music_needs_cjk(void)  { return s_cjk; }
int music_state(void)      { return s_state; }
int music_current(void)    { return s_current; }
float music_elapsed(void)  { return s_elapsed; }
float music_duration(void) { return s_duration; }
