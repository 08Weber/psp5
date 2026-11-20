/* psp5 authentic – when a sample goes to the PSP's own decoder and when a picture is shown (the decisions of video.c's hw_step(), kept here without any call of the PSP so that they can be tested).
 *
 * The decoder is some samples behind (3 for a stream with 3 reference frames) and its pictures come in display order, so with B pictures not in the order of the samples, and sometimes two in one call,
 * and three when a key frame makes it put out the pictures it held (seen on a PSP-1000, at the second key frame of a video).
 * So: the times of the samples that were given and have no picture yet wait in a set, and the picture that comes out is the one with the smallest of them; a sample is only given when no finished
 * picture is waiting for its time (the decoder runs as far ahead as it is behind); a picture is shown at its own time; the pictures before the place a jump asked for are not even converted; and at the
 * end, when the samples are used up and pictures are still inside the decoder, a key frame is given again (a decoder has to put out what it holds when a key frame starts a new sequence) until they are out. */
#ifndef HWPIPE_H
#define HWPIPE_H

#define HWP_PEND 12 /* samples the decoder may be behind */
#define HWP_OUT 4   /* converted pictures waiting for their time */
#define HWP_PIC 3   /* the pictures of one call that get a buffer (the others are counted: they are shown by no one) */
#define HWP_FLUSH_MAX 6

typedef struct {
    int buf;
    double pts;
} HwOut;

typedef struct {
    double pend[HWP_PEND]; /* the times of the samples given to the decoder, without a picture yet */
    int npend;
    HwOut out[HWP_OUT];    /* pictures that are converted and wait for their time, the earliest first */
    int nout;
    int nbuf;              /* the picture buffers there are (0 .. nbuf - 1) */
    int flush;             /* key frames given at the end to push the pictures out */
} HwPipe;

typedef enum { HWP_WAIT, HWP_PUBLISH, HWP_FEED, HWP_FLUSH, HWP_END } HwAct;

void hwp_init(HwPipe *p, int nbuf);
void hwp_clear(HwPipe *p); /* a new place of the video: nothing is waiting */

/* what to do now: `left` is the number of samples still to be given, clock the time of the sound in seconds */
HwAct hwp_act(const HwPipe *p, double clock, int left);

/* the picture to show (after HWP_PUBLISH) */
HwOut hwp_pop_out(HwPipe *p);

/* Before a sample goes to the decoder. pts_in is its time (a negative one: a key frame given again at the end, nothing joins). dst[k] is the buffer that picture k of this call (k = 0 .. HWP_PIC - 1) will be converted
 * into, or -1 when it is not wanted (there is none, it is before skip_until, or no buffer is free). front and rdy are the buffers the screen has (-1 when none). */
void hwp_plan(HwPipe *p, double pts_in, double skip_until, int front, int rdy, int dst[HWP_PIC]);

/* After the call: `pics` pictures came out (in display order). Each takes the smallest of the waiting times; those that were planned join the queue. */
void hwp_done(HwPipe *p, int pics, const int dst[HWP_PIC], double skip_until);

/* the time of the picture the next call will most likely give (for the log), or -1 */
double hwp_front_pending(const HwPipe *p);

#endif
