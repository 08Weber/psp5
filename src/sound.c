/* psp5 – made by Mille Weber */
/* Gränssnittsljud i stil med moderna konsolmenyer: mjuka, korta och luftiga.
 * Allt syntetiseras här (sinustoner, filtrerat brus, envelopes) – inga ljudfiler. */
#include <pspkernel.h>
#include <pspaudio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sound.h"

#define RATE 44100
#define BLOCK 512 /* stereo-par per utmatning, ~12 ms */
#define VOICES 4
#define PI 3.14159265f

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
/* två buffertar: den ena spelas medan nästa fylls – annars skrivs ljudet över
 * mitt i uppspelningen och små bitar upprepas */
static short __attribute__((aligned(64))) s_out[2][BLOCK * 2];

/* --- syntes --- */

static unsigned s_seed = 0x1234567;
static float noise(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return (int)(s_seed >> 9) / 4194304.0f - 1.0f;
}

/* mjuk attack, exponentiell avklingning */
static float env(float t, float attack, float tau)
{
    if (t < 0)
        return 0;
    float a = t < attack ? t / attack : 1.0f;
    return a * expf(-(t - (t < attack ? t : attack)) / tau);
}

static float tone(float t, float freq)
{
    return sinf(2 * PI * freq * t);
}

/* Fyller en buffert med dur sekunder ljud från gen(t), normaliserat till peak. */
static void render(SoundId id, float dur, float peak, float (*gen)(float t))
{
    int n = (int)(dur * RATE);
    float *tmp = malloc(n * sizeof(float));
    short *out = malloc(n * sizeof(short));
    if (!tmp || !out) {
        free(tmp);
        free(out);
        return;
    }
    float mx = 1e-6f;
    for (int i = 0; i < n; i++) {
        tmp[i] = gen((float)i / RATE);
        if (fabsf(tmp[i]) > mx)
            mx = fabsf(tmp[i]);
    }
    for (int i = 0; i < n; i++) {
        float fade = i > n - 64 ? (n - i) / 64.0f : 1.0f; /* inga klick i slutet */
        out[i] = (short)(tmp[i] / mx * peak * fade * 32767.0f);
    }
    free(tmp);
    s_samples[id].data = out;
    s_samples[id].len = n;
}

/* bläddra: kort, ljust "tick" med en mjuk kropp under */
static float gen_move(float t)
{
    float click = noise() * env(t, 0.0004f, 0.0015f) * 0.35f;
    return tone(t, 2600) * env(t, 0.0008f, 0.006f) * 0.7f + tone(t, 1300) * env(t, 0.001f, 0.014f) * 0.5f + click;
}

/* byta flik: luftigt swish – brus genom ett bandpass som sveper uppåt */
static float s_bp_lo, s_bp_band;
static float gen_swipe(float t)
{
    float dur = 0.24f;
    float f = 600 + 2600 * (t / dur);
    float c = 2 * sinf(PI * f / RATE);
    float in = noise();
    /* tillståndsvariabelfilter (bandpass) */
    float hi = in - s_bp_lo - 0.5f * s_bp_band;
    s_bp_band += c * hi;
    s_bp_lo += c * s_bp_band;
    float shape = sinf(PI * fminf(t / dur, 1.0f));
    return s_bp_band * shape * shape + tone(t, 1760 + 600 * t / dur) * env(t, 0.03f, 0.05f) * 0.08f;
}

/* välja: två ljusa toner (C6, G6) med en liten fördröjning */
static float gen_select(float t)
{
    return tone(t, 1046.5f) * env(t, 0.003f, 0.09f) + tone(t, 1568.0f) * env(t - 0.045f, 0.003f, 0.11f) * 0.8f +
           tone(t, 3136.0f) * env(t - 0.045f, 0.002f, 0.03f) * 0.15f;
}

/* tillbaka: samma klang men nedåt (E6 → B5) och lite mörkare */
static float gen_back(float t)
{
    return tone(t, 1318.5f) * env(t, 0.003f, 0.06f) * 0.8f + tone(t, 987.8f) * env(t - 0.04f, 0.003f, 0.09f);
}

/* reglage: två snabba ticks */
static float gen_toggle(float t)
{
    return tone(t, 1900) * env(t, 0.0006f, 0.008f) + tone(t, 2850) * env(t - 0.035f, 0.0006f, 0.008f) * 0.7f;
}

/* öppna inställningar: kort uppåtgående arpeggio (G5, B5, D6) */
static float gen_open(float t)
{
    return tone(t, 784.0f) * env(t, 0.004f, 0.12f) * 0.8f + tone(t, 987.8f) * env(t - 0.05f, 0.004f, 0.12f) * 0.8f +
           tone(t, 1174.7f) * env(t - 0.10f, 0.004f, 0.16f);
}

/* starta spel: ett mjukt ackord som sväller upp, med ett luftigt skimmer ovanpå */
static float gen_launch(float t)
{
    static const float notes[] = { 523.3f, 659.3f, 784.0f, 1046.5f, 1318.5f };
    float swell = (t < 0.18f ? t / 0.18f : expf(-(t - 0.18f) / 0.35f));
    float sum = 0;
    for (int i = 0; i < 5; i++)
        sum += tone(t, notes[i] * (1 + 0.002f * i)) * (i < 4 ? 1.0f : 0.5f);
    float air = noise() * 0.12f * swell * (0.5f + 0.5f * sinf(2 * PI * 7 * t));
    return sum * swell * 0.25f + air;
}

/* fel: dov, mjuk dubbelpuls */
static float gen_error(float t)
{
    float s = tone(t, 330) + tone(t, 990) * 0.25f;
    return s * (env(t, 0.004f, 0.04f) + env(t - 0.09f, 0.004f, 0.05f));
}

static void build_sounds(void)
{
    render(SND_MOVE, 0.06f, 0.22f, gen_move);
    s_bp_lo = s_bp_band = 0;
    render(SND_SWIPE, 0.26f, 0.20f, gen_swipe);
    render(SND_SELECT, 0.42f, 0.28f, gen_select);
    render(SND_BACK, 0.32f, 0.24f, gen_back);
    render(SND_TOGGLE, 0.08f, 0.22f, gen_toggle);
    render(SND_OPEN, 0.50f, 0.26f, gen_open);
    render(SND_LAUNCH, 1.10f, 0.32f, gen_launch);
    render(SND_ERROR, 0.26f, 0.24f, gen_error);
    s_built = 1;
}

/* --- mixer --- */

static int audio_thread(SceSize args, void *argp)
{
    /* ljuden räknas fram här, i bakgrunden, så att starten inte blir långsammare */
    if (!s_built)
        build_sounds();
    int buf = 0, tail = 0;
    while (!s_quit) {
        int any = 0;
        for (int v = 0; v < VOICES; v++)
            any |= s_voices[v].active;
        if (any) {
            tail = 8; /* efter sista ljudet: några block tystnad (~0,1 s) */
        } else if (tail > 0) {
            tail--; /* kanalen ska sluta på tystnad, inte på en bit av ljudet */
        } else {
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
            if (mix > 32767) mix = 32767;
            if (mix < -32768) mix = -32768;
            out[i * 2] = out[i * 2 + 1] = (short)mix;
        }
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
    }
    s_built = 0;
}

void sound_play(SoundId id)
{
    if (!g_sound_on || s_thread < 0 || !s_built || id >= SND_COUNT)
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
    static const char *names[SND_COUNT] = { "move", "swipe", "select", "back", "toggle", "open", "launch", "error" };
    while (!s_built)
        sceKernelDelayThread(10000);
    sceIoMkdir(dir, 0777);
    for (int i = 0; i < SND_COUNT; i++) {
        char path[200];
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
