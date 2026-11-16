/* psp5 – made by 08Weber */
/* Gränssnittsljud från PS5: förpackade i src/ps5_sounds.h (zlib-komprimerad 44,1 kHz
 * 16-bitars mono-PCM) och uppackade en gång i bakgrunden vid start. */
#include <pspkernel.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "sound.h"
#include "config.h"
#include "ps5_sounds.h"

#define RATE PS5_SND_RATE
#define BLOCK 512 /* stereo-par per utmatning, ~12 ms */
#define VOICES 6

typedef struct {
    short *data;
    int len;
} Sample;

typedef struct {
    const short *data;
    int len;
    volatile int pos;
    volatile int active;
} Voice;

unsigned char g_sound_on = 1;

static Sample s_samples[SND_COUNT];
static Voice s_voices[VOICES];
static SceUID s_thread = -1;
static int s_channel = -1;
static volatile int s_quit;
static volatile int s_built;
static volatile int s_fade;     /* >0: block kvar av uttoningen */
static volatile int s_boot_req; /* startljudet ska packas upp igen */
static int s_skip_boot;         /* felsäker start: ingen startskärm */
/* två buffertar: den ena spelas medan nästa fylls – annars skrivs ljudet över
 * mitt i uppspelningen och små bitar upprepas */
static short __attribute__((aligned(64))) s_out[2][BLOCK * 2];

static void load_asset(SoundId id)
{
    if (id < 0 || id >= SND_COUNT || s_samples[id].data)
        return;
    const Ps5SoundAsset *a = &PS5_ASSETS[id];
    uLongf dst_len = (uLongf)(a->samples * (int)sizeof(short));
    short *out = malloc(dst_len);
    if (!out)
        return;
    if (uncompress((Bytef *)out, &dst_len, a->z, (uLong)a->zlen) != Z_OK) {
        free(out);
        return;
    }
    s_samples[id].data = out;
    s_samples[id].len = (int)(dst_len / sizeof(short));
}

static void build_sounds(void)
{
    /* startljudet behövs bara första gången (och frigörs efter startskärmen) */
    static int boot_done;
    if (!boot_done && g_boot_anim && !s_skip_boot)
        load_asset(SND_BOOT);
    boot_done = 1;
    for (int i = 0; i < SND_BOOT; i++)
        load_asset((SoundId)i);
    s_built = 1;
}

/* --- mixer --- */

/* Mjuk toppbegränsning över 80 % när flera starka PS5-ljud överlappar i mixern. */
static short mix_limit(int x)
{
    const int knee = 26214;
    int ax = x < 0 ? -x : x;
    if (ax <= knee)
        return (short)x;
    int over = ax - knee;
    int y = knee + (over * 6553) / (over + 6553);
    return (short)(x < 0 ? -y : y);
}

static int audio_thread(SceSize args, void *argp)
{
    /* ljuden packas upp här, i bakgrunden, så att starten inte blir långsammare */
    if (!s_built)
        build_sounds();
    int buf = 0, tail = 0;
    while (!s_quit) {
        if (s_boot_req) { /* startskärmen efter viloläge – packas upp igen i bakgrunden */
            load_asset(SND_BOOT);
            s_boot_req = 0;
        }
        int any = 0;
        for (int v = 0; v < VOICES; v++)
            any |= s_voices[v].active;
        if (any) {
            tail = 8; /* efter sista ljudet: några block tystnad (~0,1 s) */
        } else if (tail > 0) {
            tail--; /* kanalen ska sluta på tystnad, inte på en bit av ljudet */
        } else {
            s_fade = 0; /* inget spelas – en begärd uttoning gäller inte nästa ljud */
            sceKernelDelayThread(4000);
            continue;
        }
        short *out = s_out[buf];
        buf ^= 1;
        for (int i = 0; i < BLOCK; i++) {
            int mix = 0;
            for (int v = 0; v < VOICES; v++) {
                Voice *vo = &s_voices[v];
                if (!vo->active)
                    continue;
                int pos = vo->pos;
                if (pos >= vo->len) {
                    vo->active = 0;
                    continue;
                }
                mix += vo->data[pos];
                vo->pos = pos + 1;
            }
            if (s_fade > 0)
                mix = mix * (s_fade * BLOCK - i) / (24 * BLOCK);
            out[i * 2] = out[i * 2 + 1] = mix_limit(mix);
        }
        if (s_fade > 0 && --s_fade == 0)
            for (int v = 0; v < VOICES; v++)
                s_voices[v].active = 0;
        sceAudioOutputPannedBlocking(s_channel, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, out);
    }
    return 0;
}

void sound_init(void)
{
    s_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, BLOCK, PSP_AUDIO_FORMAT_STEREO);
    if (s_channel < 0)
        return;
    s_quit = 0;
    memset(s_voices, 0, sizeof(s_voices));
    s_thread = sceKernelCreateThread("psp5_audio", audio_thread, 0x12, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (s_thread >= 0)
        sceKernelStartThread(s_thread, 0, NULL);
}

void sound_term(void)
{
    if (s_thread >= 0) {
        s_quit = 1;
        sceKernelWaitThreadEnd(s_thread, NULL);
        sceKernelDeleteThread(s_thread);
        s_thread = -1;
    }
    if (s_channel >= 0) {
        sceAudioChRelease(s_channel);
        s_channel = -1;
    }
    for (int i = 0; i < SND_COUNT; i++) {
        free(s_samples[i].data);
        s_samples[i].data = NULL;
        s_samples[i].len = 0;
    }
    s_built = 0;
}

void sound_play(SoundId id)
{
    if (!g_sound_on || s_thread < 0 || !s_built || id < 0 || id >= SND_COUNT || !s_samples[id].data || !s_samples[id].len)
        return;
    /* samma ljud igen (t.ex. snabb bläddring): starta om i samma röst */
    Voice *pick = NULL;
    for (int v = 0; v < VOICES && !pick; v++)
        if (s_voices[v].active && s_voices[v].data == s_samples[id].data)
            pick = &s_voices[v];
    for (int v = 0; v < VOICES && !pick; v++)
        if (!s_voices[v].active)
            pick = &s_voices[v];
    if (!pick)
        pick = &s_voices[0];
    /* rösten stängs först så att mixern aldrig läser en halvt bytt röst */
    pick->active = 0;
    pick->pos = 0;
    pick->data = s_samples[id].data;
    pick->len = s_samples[id].len;
    pick->active = 1;
}

int sound_ready(void)
{
    return (s_built && !s_boot_req) || s_thread < 0;
}

void sound_prepare_boot(void)
{
    if (g_sound_on && s_thread >= 0 && !s_samples[SND_BOOT].data)
        s_boot_req = 1;
}

void sound_skip_boot(void)
{
    s_skip_boot = 1;
}

void sound_fade_out(void)
{
    s_fade = 24; /* ~0,3 s */
}

void sound_release(SoundId id)
{
    if (id < 0 || id >= SND_COUNT || !s_samples[id].data)
        return;
    for (int v = 0; v < VOICES; v++)
        if (s_voices[v].data == s_samples[id].data)
            s_voices[v].active = 0;
    sceKernelDelayThread(30000); /* mixern kan vara mitt i ett block */
    free(s_samples[id].data);
    s_samples[id].data = NULL;
    s_samples[id].len = 0;
}

void sound_wait(int timeout_ms)
{
    for (int t = 0; t < timeout_ms && s_thread >= 0; t += 10) {
        int any = 0;
        for (int v = 0; v < VOICES; v++)
            any |= s_voices[v].active;
        if (!any)
            return;
        sceKernelDelayThread(10000);
    }
}

#ifdef PSP5_TEST
void sound_dump(const char *dir)
{
    static const char *names[SND_COUNT] = {
        "move", "swipe", "panel", "select", "back", "toggle_on", "toggle_off", "slider",
        "open", "close_menu", "cc_open", "cc_close", "launch", "error", "screenshot",
        "key_move", "key_type", "key_back", "key_error", "confirm", "boot"
    };
    while (!s_built)
        sceKernelDelayThread(10000);
    sceIoMkdir(dir, 0777);
    for (int i = 0; i < SND_COUNT; i++) {
        char path[200];
        if (!s_samples[i].data)
            continue;
        snprintf(path, sizeof(path), "%s/%s.wav", dir, names[i]);
        SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
        if (fd < 0)
            continue;
        int bytes = s_samples[i].len * 2;
        unsigned hdr[11] = { 0x46464952, 36 + bytes, 0x45564157, 0x20746D66, 16, 0x00010001, RATE, RATE * 2,
                             0x00100002, 0x61746164, bytes };
        sceIoWrite(fd, hdr, sizeof(hdr));
        sceIoWrite(fd, s_samples[i].data, bytes);
        sceIoClose(fd);
    }
}
#endif
