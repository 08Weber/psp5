/* psp5 – made by 08Weber */
/* UI sound mixer using embedded Deck-UI_Sound assets (44.1 kHz mono 4-bit IMA ADPCM).
 * Decoded on-the-fly in the mixer thread with zero heap allocations. */
#include <pspkernel.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sound.h"
#include "deck_sounds.h"

#define RATE   44100
#define BLOCK  512 /* stereo sample pairs per output block (~11.6 ms) */
#define VOICES 8

typedef struct {
    const unsigned char *data;
    int samples;
    int pos;
    int pred;
    int step_idx;
    int gain_q8;
    volatile int active;
} Voice;

unsigned char g_sound_on = 1;

static Voice s_voices[VOICES];
static u32 s_last_play[SND_COUNT];
static SceUID s_thread = -1;
static int s_channel = -1;
static volatile int s_quit;
static volatile int s_fade; /* >0: blocks remaining in fade-out */
static int s_skip_boot;
/* Double buffer: one plays via sceAudio while the next is mixed */
static short __attribute__((aligned(64))) s_out[2][BLOCK * 2];

static const signed char s_ima_index[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static const short s_ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static inline int ima_decode_sample(Voice *vo, int pos)
{
    unsigned char b = vo->data[pos >> 1];
    int nibble = (pos & 1) ? (b >> 4) : (b & 0x0F);
    int step = s_ima_step[vo->step_idx];
    int diff = step >> 3;
    if (nibble & 4)
        diff += step;
    if (nibble & 2)
        diff += step >> 1;
    if (nibble & 1)
        diff += step >> 2;
    int pred = vo->pred + ((nibble & 8) ? -diff : diff);
    if (pred > 32767)
        pred = 32767;
    else if (pred < -32768)
        pred = -32768;
    int si = vo->step_idx + s_ima_index[nibble];
    if (si < 0)
        si = 0;
    else if (si > 88)
        si = 88;
    vo->pred = pred;
    vo->step_idx = si;
    return (pred * vo->gain_q8) >> 8;
}

static int audio_thread(SceSize args, void *argp)
{
    int buf = 0, tail = 0;
    while (!s_quit) {
        int any = 0;
        for (int v = 0; v < VOICES; v++)
            any |= s_voices[v].active;
        if (any) {
            tail = 8; /* ~0.1 s of silence tail after the last voice finishes */
        } else if (tail > 0) {
            tail--;
        } else {
            s_fade = 0;
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
                if (pos >= vo->samples) {
                    vo->active = 0;
                    continue;
                }
                mix += ima_decode_sample(vo, pos);
                vo->pos = pos + 1;
            }
            if (s_fade > 0)
                mix = mix * (s_fade * BLOCK - i) / (24 * BLOCK);
            if (mix > 32767)
                mix = 32767;
            if (mix < -32768)
                mix = -32768;
            out[i * 2] = out[i * 2 + 1] = (short)mix;
        }
        if (s_fade > 0 && --s_fade == 0) {
            for (int v = 0; v < VOICES; v++)
                s_voices[v].active = 0;
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
    s_fade = 0;
    memset(s_voices, 0, sizeof(s_voices));
    memset(s_last_play, 0, sizeof(s_last_play));
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
}

static u32 cooldown_us(SoundId id)
{
    switch (id) {
    case SND_VOLUME:
        return 90000u;
    case SND_MOVE:
    case SND_TILE:
    case SND_TYPE:
    case SND_SLIDER_UP:
    case SND_SLIDER_DN:
        return 25000u;
    default:
        return 0u;
    }
}

void sound_play(SoundId id)
{
    if (!g_sound_on || s_thread < 0 || id < 0 || id >= SND_COUNT)
        return;
    if (id == SND_BOOT && s_skip_boot)
        return;
    const DeckSoundMeta *m = &s_deck_meta[id];
    if (!m->samples)
        return;

    u32 cd = cooldown_us(id);
    if (cd > 0) {
        u32 now = sceKernelGetSystemTimeLow();
        if (s_last_play[id] != 0 && (u32)(now - s_last_play[id]) < cd)
            return;
        s_last_play[id] = now ? now : 1;
    }

    const unsigned char *data = s_deck_blob + m->offset;
    Voice *pick = NULL;
    /* Reuse same-sound voice on rapid navigation so ticks stay crisp */
    for (int v = 0; v < VOICES && !pick; v++)
        if (s_voices[v].active && s_voices[v].data == data)
            pick = &s_voices[v];
    for (int v = 0; v < VOICES && !pick; v++)
        if (!s_voices[v].active)
            pick = &s_voices[v];
    if (!pick) {
        /* Steal the voice closest to completion */
        int best_rem = 0x7fffffff;
        for (int v = 0; v < VOICES; v++) {
            int rem = s_voices[v].samples - s_voices[v].pos;
            if (rem < best_rem) {
                best_rem = rem;
                pick = &s_voices[v];
            }
        }
    }
    if (!pick)
        pick = &s_voices[0];

    /* Clear active first with compiler barrier so audio_thread never observes partial voice state */
    pick->active = 0;
    __asm__ volatile("" ::: "memory");
    pick->data = data;
    pick->samples = m->samples;
    pick->pos = 0;
    pick->pred = 0;
    pick->step_idx = 0;
    pick->gain_q8 = m->gain_q8;
    __asm__ volatile("" ::: "memory");
    pick->active = 1;
}

int sound_ready(void)
{
    return 1;
}

void sound_prepare_boot(void)
{
}

void sound_skip_boot(void)
{
    s_skip_boot = 1;
}

void sound_fade_out(void)
{
    s_fade = 24; /* ~0.3 s */
}

void sound_release(SoundId id)
{
    (void)id;
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
        "move", "tile", "bumper", "swipe", "select", "back", "toggle", "toggle_off",
        "slider_up", "slider_dn", "open", "close", "side_in", "side_out",
        "detail_in", "detail_out", "type", "toast", "msg_toast", "volume",
        "music_act", "launch", "error", "boot"
    };
    sceIoMkdir(dir, 0777);
    for (int i = 0; i < SND_COUNT; i++) {
        char path[200];
        const DeckSoundMeta *m = &s_deck_meta[i];
        if (!m->samples)
            continue;
        short *pcm = malloc((size_t)m->samples * sizeof(short));
        if (!pcm)
            continue;
        Voice vo = { s_deck_blob + m->offset, m->samples, 0, 0, 0, m->gain_q8, 1 };
        for (int s = 0; s < m->samples; s++)
            pcm[s] = (short)ima_decode_sample(&vo, s);
        snprintf(path, sizeof(path), "%s/%s.wav", dir, names[i]);
        SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
        if (fd >= 0) {
            int bytes = m->samples * 2;
            unsigned hdr[11] = { 0x46464952, 36 + bytes, 0x45564157, 0x20746D66, 16, 0x00010001, RATE, RATE * 2,
                                 0x00100002, 0x61746164, bytes };
            sceIoWrite(fd, hdr, sizeof(hdr));
            sceIoWrite(fd, pcm, bytes);
            sceIoClose(fd);
        }
        free(pcm);
    }
}
#endif
