/* psp5 authentic – slowing sound down without lowering its pitch (the software video, when the PSP cannot decode as fast as the video plays and the video goes slower).
 *
 * Overlap and add with a search for the best place (WSOLA): the sound is cut into overlapping pieces of 1024 frames, each is faded in over the 512 before it and out over the 512 after it, and the
 * pieces are put together at a distance of 512 frames, but taken from the input at a distance of 512 * speed, so that the sound is longer by 1 / speed. Each piece starts where the input has what
 * the last piece would have gone on with (searched within +-64 frames), so the waves join: the pitch is that of the input. Stereo, 16 bits. */
#ifndef TSCALE_H
#define TSCALE_H

#define TS_HOP 512   /* frames of output a call makes */
#define TS_DELTA 64  /* how far a piece may start from where it should (frames) */
#define TS_WINDOW (2 * TS_DELTA + 2 * TS_HOP + 16) /* the frames of input a call looks at (from ts_low()) */

typedef struct {
    float ola[TS_HOP * 2]; /* the second half of the last piece, faded out (stereo, interleaved) */
    long long prev_start;  /* where the last piece began in the input (absolute frame number) */
    double nom;            /* where the next piece should begin */
    int started;
} TScale;

/* starts at frame `at` of the input (a frame number counted from the start of the input: the sound goes on from there as it was) */
void ts_begin(TScale *t, long long at);
/* the lowest frame of the input the next call reads (the input in front of it is not needed any more) */
long long ts_low(const TScale *t);
/* Makes TS_HOP frames of output into out (stereo) from in, which holds TS_WINDOW frames of the input from ts_low(); ha is how far the input goes on for them (TS_HOP * speed). t->nom is where the
 * input has got to, when it returns (the position the sound is at). */
void ts_hop(TScale *t, const short *in, double ha, short *out);

#endif
