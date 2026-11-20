/* psp5 authentic – the timing the PSP's own H.264 streams have, written into a stream that has none.
 *
 * The PSP's sceMpeg library gets its pictures from streams made by Sony's tools. In those, the SPS has a VUI with timing and HRD parameters (NAL and VCL, the same), and every picture has a
 * "picture timing" SEI message (how long it waits to be decoded and to be shown), every key frame also a "buffering period" and a "recovery point" message. Streams without all that
 * (what x264 and other ordinary encoders make) froze the PSP or gave the decoder's fatal error. The PSP's XMB plays such files by another route (sceMp4), which does not need them.
 * Everything here is read off the PMF of PPSSPP's tests (see DEVELOPER-NOTES) and gives the same bytes as that stream has. */
#ifndef H264SEI_H
#define H264SEI_H

#include <stdint.h>

/* The clock tick of the stream: num_units / time_scale seconds. The PSP's own streams count in fields: 4004 / 240000 for 29.97 pictures a second (one tick = one field). */
typedef struct {
    unsigned num_units, time_scale;
    int to_main; /* an SPS of the Baseline profile is made one of the Main profile (see h264_sps_rewrite) */
} H264Tick;

/* 1 when the SPS (a NAL unit with its header byte, no start code) announces HRD parameters: such a stream is made the way the PSP's own are and is left as it is. 0 when it does not (or
 * cannot be read). */
int h264_sps_has_hrd(const uint8_t *nal, int len);

/* The SPS with the VUI of the PSP's own streams (timing of `tick`, NAL and VCL HRD, picture structure; the aspect ratio, range and colours of the stream are kept) instead of the one it has;
 * everything before the VUI is as it was, except that with tick.to_main a Baseline SPS (profile 66) becomes a Main one (profile 77, the constraint flags of the PSP's own streams, a level of at least
 * 2.1): every Baseline stream without slice groups and redundant slices is a Main stream too, and the PSP's sceMpeg library gave the decoder's error for Baseline and none for Main.
 * The result is a NAL unit too (same header byte). Returns its length, -1 when the SPS cannot be read or the result does not fit in cap. */
int h264_sps_rewrite(const uint8_t *nal, int len, H264Tick tick, uint8_t *out, int cap);

/* The SPS with only its profile changed as h264_sps_rewrite does it for tick.to_main (a stream that has the PSP's timing already but is Baseline); anything but a Baseline SPS is copied as it is.
 * Returns the length of the NAL unit written to out, -1 on an error. */
int h264_sps_relabel(const uint8_t *nal, int len, uint8_t *out, int cap);

/* The SPS with only its profile changed to Baseline (profile 66, constraint flags 0xC0; the level stays): for a Main or Baseline SPS (the fields of both are the same; the PSP's decoder in its
 * Baseline mode, hwavc.h); anything else is copied as it is. Returns the length of the NAL unit written to out, -1 on an error. */
int h264_sps_to_base(const uint8_t *nal, int len, uint8_t *out, int cap);

/* 1 when the PPS (NAL unit with its header byte) uses slice groups (FMO: Baseline only) or cannot be read; *cabac gets the entropy coding mode (1 = CABAC, 0 = CAVLC), *nref0 the number of
 * reference pictures a slice has by default and *weighted whether weighted prediction is on */
int h264_pps_info(const uint8_t *nal, int len, int *cabac, int *nref0, int *weighted);

/* what an SPS says of the stream: the profile, the number of reference frames, the size in macroblocks and the picture order count type; 0 when it cannot be read */
int h264_sps_info(const uint8_t *nal, int len, int *profile, int *nref, int *wmb, int *hmb, int *poc_type);

/* SEI NAL units (header byte included, no start code); each returns its length or -1 */
int h264_sei_buffering(uint8_t *out, int cap);                                                 /* buffering period of a key frame: the first delay is one second (90000) */
int h264_sei_pic_timing(unsigned cpb_removal_delay, unsigned dpb_output_delay, uint8_t *out, int cap); /* in ticks: the decoding time since the key frame, the time from decoding to showing */
int h264_sei_recovery(uint8_t *out, int cap);                                                  /* recovery point: decoding can start here */

#endif
