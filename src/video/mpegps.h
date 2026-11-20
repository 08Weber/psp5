/* psp5 authentic – the pieces that turn the samples of an MP4 into what the PSP's decoders take: H.264 as an Annex B stream with its parameter sets, AAC frames with an ADTS header, and the
 * video as an MPEG program stream in 2048 byte sectors (the way the PSP's own PMF files are cut), which the PSP's sceMpeg library reads from its ring buffer. */
#ifndef MPEGPS_H
#define MPEGPS_H

#include <stdint.h>
#include "h264sei.h"

/* What makes a stream that has none of the timing of the PSP's own into one that has it (see h264sei.h): the clock tick (to rewrite an SPS in the sample), and in ticks the time the unit waits
 * to be decoded (counted from the key frame) and the time from its decoding to its showing. */
typedef struct {
    H264Tick tick;      /* with tick.to_main a Baseline SPS becomes a Main one */
    int timing;         /* 1: the SPS gets the PSP's VUI and the SEI messages are replaced; 0: only the profile of a Baseline SPS is changed */
    unsigned cpb_removal_delay, dpb_output_delay;
} PsSynth;

/* One H.264 sample (NAL units, each with a length of nal_size bytes in front) -> Annex B in out: an access unit delimiter, the parameter sets (params, of n bytes, when this is a key
 * frame) and the NAL units each behind a start code. Returns the bytes written, or -1 when it does not fit in cap or the sample is damaged. out must not be the sample.
 * With `synth` (and synth->timing) the sample's own SEI messages are left out, an SPS in it gets the VUI of the PSP's own streams, and the unit gets the messages of those: a picture timing before its first slice,
 * and in front of that a buffering period (and after it a recovery point) when it is a key frame (an IDR slice). `params` have to have been rewritten the same way by the caller. */
int mpegps_annexb(const uint8_t *sample, int len, int nal_size, const uint8_t *params, int params_len, int key, uint8_t *out, int cap, const PsSynth *synth);

/* 1 when the sample carries parameter sets (SPS or PPS) itself: such a stream (the PSP's own are) is not given the ones of the MP4's header, which can be older than the ones the stream changed to */
int mpegps_sample_has_params(const uint8_t *sample, int len, int nal_size);
/* 1 when the unit (Annex B) has an SPS in it: it starts a sequence, and so a group of the program stream */
int mpegps_unit_has_sps(const uint8_t *unit, int len);

/* the 7 byte ADTS header of an AAC frame of `payload` bytes, from the AudioSpecificConfig of the MP4 (AAC-LC and the other object types of 1 to 4 bits); 0 when the config is not understood */
int mpegps_adts(const uint8_t *asc, int asc_len, int payload, uint8_t out[7]);

/* The program stream writer. It makes what the PSP's own PMF files look like (read off the real test.pmf of PPSSPP's tests, whose stream it reproduces sector for sector):
 *  - the units (pictures) of a group run one after the other through 2048 byte sectors, each sector a pack header and one PES packet that is filled up; only the last sector of a group has room
 *    left (a padding packet fills it);
 *  - a group of at most PS_TABLE_MAX units starts a sector, which has the system header and a "private stream 2" packet behind its pack header: the table of the size of every unit of the group, and
 *    in four numbers the sector (counted from this one) each of the first four units ends in. The PSP's library very likely cuts the units out of the stream by that table;
 *  - the PES packet of that first sector has both time stamps (of the first unit) and the P-STD extension; a PES packet later in the group has both time stamps (no extension) when a unit starts in
 *    it and at least PS_STAMP_EVERY units have started since the last stamp (they belong to the first unit that starts in the packet); every other one has none. */
#define PS_TABLE_MAX 59
#define PS_STAMP_EVERY 16

/* unit j of the stream: its size in bytes as pull() will give it (-1: the unit is left out) and whether it is a key frame */
typedef int (*PsSize)(void *arg, int j, int *key);
/* the next unit that is not left out: its data (it must stay as it is until the unit after it has been pulled, and until the one after that is asked for: two buffers do), size and time stamps in
 * ticks of 90 kHz; 0 when there are no more */
typedef int (*PsPull)(void *arg, const uint8_t **data, int *len, int64_t *pts, int64_t *dts);

typedef struct {
    PsSize size;
    PsPull pull;
    void *arg;
    int plan_at, count;            /* the next unit to plan a group from, and the number of units */
    const uint8_t *data;           /* the unit that is being written */
    int len, pos;
    int64_t pts, dts;
    int has_ahead;                 /* the unit after it has been pulled already (to know its time stamps) */
    const uint8_t *adata;
    int alen;
    int64_t apts, adts;
    int group_left;                /* units of the group that are not pulled yet */
    int new_group;                 /* the next sector starts a group */
    int table_n;
    uint16_t table[PS_TABLE_MAX];
    uint16_t words[4];
    int cur_idx, last_stamp_idx;   /* the number of the unit that is written and of the last one that got a stamp */
    int cur_starts;                /* the unit that is written starts at the start of the sector */
    int64_t scr;
} PsWriter;

/* units first .. count-1 are written, grouped by planning with size() and pulled with pull() */
void ps_init(PsWriter *w, int first, int count, PsSize size, PsPull pull, void *arg);
/* the next sector of the stream; 0 when there are no more units */
int ps_sector(PsWriter *w, uint8_t out[2048]);

#endif
