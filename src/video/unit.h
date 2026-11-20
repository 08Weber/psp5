/* psp5 authentic – the units (pictures) of an MP4 as the PSP's sceMpeg library wants them in its program stream: Annex B, laid out like the PSP's own streams, and, for a stream that has none of
 * the timing those have, with that timing written in (see h264sei.h). Used by the video player and by the tests. */
#ifndef UNIT_H
#define UNIT_H

#include <stdint.h>
#include "mp4.h"
#include "h264sei.h"

typedef struct {
    Mp4 *m;
    uint8_t *sample; /* room for the biggest sample of the video: the caller's */
    int cap;
    uint8_t params[512]; /* the parameter sets of the MP4's header as Annex B (the SPS rewritten when the timing is written in) */
    int params_len;
    int inband; /* the parameter sets are in the samples of the video (the first one has them) */
    int synth;  /* the stream is given the timing of the PSP's own */
    int no_synth, no_main, force; /* test switches (PSP5/video-opts.txt): leave the timing out / leave a Baseline SPS as it is / try a stream that is known not to decode */
    int profile;
    int unsupported; /* 0, or why the PSP's decoder cannot take the stream: 1 = CAVLC (Baseline entropy coding), 2 = a High profile */
    int nref, wmb, hmb, poc, cabac, nref0, weighted; /* what the SPS and the PPS that are given to the decoder say (0 when the stream carries them in its samples) */
    int to_main; /* ... and its Baseline SPS is made a Main one */
    int hw_ok;   /* the stream may be tried with the PSP's decoder in its Baseline mode (hwavc.h): CAVLC, no slice groups, Main or Baseline profile, picture order type 2 (no B pictures) */
    int hw_main; /* ... or Main profile, no slice groups, with CABAC or B pictures (what the Baseline mode does not take): the PSP's decoder in its Main mode may be tried (hwavc.h) */
    H264Tick tick;
    double tick_s;
} UnitMaker;

/* the switches no_synth and no_main have to be set before unit_init() when they are wanted */
/* reads the parameter sets, finds out whether the stream carries them itself and whether it has the PSP's timing; sample is room for the biggest sample (cap bytes). 1 when it can be made, 0 when not. */
int unit_init(UnitMaker *u, Mp4 *m, uint8_t *sample, int cap);

/* the unit for video sample i in out: its size in bytes, or -1 when the sample is left out (it cannot be read or understood). *group_start is 1 when the unit has an SPS: it starts a group. */
int unit_make(UnitMaker *u, int i, uint8_t *out, int out_cap, int *group_start);

#endif
