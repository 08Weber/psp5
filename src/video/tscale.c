/* psp5 authentic – slowing sound down without lowering its pitch (see tscale.h) */
#include "tscale.h"

#include <math.h>
#include <string.h>

static float g_up[TS_HOP]; /* the fade in over the first half of a piece; the fade out is 1 minus it (the two make one where pieces that are the same overlap) */
static int g_ready;

static void tables(void)
{
    if (g_ready)
        return;
    for (int i = 0; i < TS_HOP; i++) {
        float s = sinf(3.14159265f * ((float)i + 0.5f) / (2.0f * TS_HOP));
        g_up[i] = s * s;
    }
    g_ready = 1;
}

void ts_begin(TScale *t, long long at)
{
    tables();
    memset(t, 0, sizeof(*t));
    t->nom = (double)at;
}

long long ts_low(const TScale *t)
{
    return (long long)floor(t->nom) - TS_DELTA - 4;
}

static inline int mono(const short *p) { return (p[0] + p[1]) >> 1; }

void ts_hop(TScale *t, const short *in, double ha, short *out)
{
    tables();
    long long low = ts_low(t);
    long long nomf = (long long)floor(t->nom);
    if (!t->started) { /* as if a piece began one hop ago: its second half, faded out, is what the sound goes on with here */
        t->started = 1;
        t->prev_start = nomf - TS_HOP;
        const short *p = in + (nomf - low) * 2;
        for (int i = 0; i < TS_HOP; i++) {
            float down = 1.0f - g_up[i];
            t->ola[i * 2] = (float)p[i * 2] * down;
            t->ola[i * 2 + 1] = (float)p[i * 2 + 1] * down;
        }
    }
    /* where the last piece would have gone on: the best start is the one whose beginning looks most like that (compared on every fourth frame, mono). Coarse to fine: every eighth start (and every
     * eighth frame) first, then the two starts four frames either side of the best one on every fourth frame: the same starts as every fourth one, a third of the comparisons. */
    const short *nat = in + (t->prev_start + TS_HOP - low) * 2;
    int nm[TS_HOP / 4]; /* the mono samples of nat, every fourth frame */
    for (int j = 0; j < TS_HOP / 4; j++)
        nm[j] = mono(nat + 4 * j * 2);
    int best_d = 0;
    long long best = -1;
    for (int d = -TS_DELTA; d <= TS_DELTA; d += 8) {
        const short *c = in + (nomf + d - low) * 2;
        long long sad = 0;
        for (int j = 0; j < TS_HOP / 4; j += 2) {
            int diff = nm[j] - mono(c + 4 * j * 2);
            sad += diff < 0 ? -diff : diff;
        }
        sad *= 2; /* (half the frames: the same scale as the finer comparison) */
        if (d == 0)
            sad -= sad >> 4; /* (a small preference for not moving) */
        if (best < 0 || sad < best) {
            best = sad;
            best_d = d;
        }
    }
    {
        int centre = best_d;
        long long fine_best = -1;
        for (int d = centre - 4; d <= centre + 4; d += 4) {
            if (d < -TS_DELTA || d > TS_DELTA)
                continue;
            const short *c = in + (nomf + d - low) * 2;
            long long sad = 0;
            for (int j = 0; j < TS_HOP / 4; j++) {
                int diff = nm[j] - mono(c + 4 * j * 2);
                sad += diff < 0 ? -diff : diff;
            }
            if (d == 0)
                sad -= sad >> 4;
            if (fine_best < 0 || sad < fine_best) {
                fine_best = sad;
                best_d = d;
            }
        }
    }
    long long start = nomf + best_d;
    const short *seg = in + (start - low) * 2;
    for (int i = 0; i < TS_HOP; i++) {
        float up = g_up[i], down = 1.0f - up;
        for (int c = 0; c < 2; c++) {
            float v = t->ola[i * 2 + c] + (float)seg[i * 2 + c] * up;
            v = v < 0 ? v - 0.5f : v + 0.5f;
            out[i * 2 + c] = (short)(v > 32767.0f ? 32767.0f : v < -32768.0f ? -32768.0f : v);
            t->ola[i * 2 + c] = (float)seg[(TS_HOP + i) * 2 + c] * down;
        }
    }
    t->prev_start = start;
    t->nom += ha;
}
