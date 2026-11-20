/* psp5 authentic – an H.264 decoder in software, for the videos the PSP's own decoder (sceMpeg) cannot take: CAVLC (what the Baseline profile has, and so what the HandBrake PSP preset and most phone
 * videos are made of) and streams with more reference frames than it allows. Constrained to what such streams use: 8-bit 4:2:0, frames (no interlacing), I and P slices (no B), one or more slices
 * per picture, any number of reference frames, the loop filter, the long-term reference tools. Not done (h264d_new gives NULL and says why): CABAC, B slices, interlaced coding, slice groups (FMO),
 * weighted prediction, the 8x8 transform and scaling matrices of the High profiles, other bit depths and chroma formats. It is written from the standard and checked against ffmpeg picture by
 * picture (host-tests). Made for the speed of a 333 MHz PSP: 480 x 272 at 30 pictures a second is the aim. */
#ifndef H264DEC_H
#define H264DEC_H

#include <stdint.h>

typedef struct H264Dec H264Dec;

/* why a stream is not decoded (h264d_new gives it in *why) */
enum { H264_OK = 0, H264_NO_PARAMS, H264_CABAC, H264_B_SLICES, H264_INTERLACED, H264_FMO, H264_WEIGHTED, H264_HIGH_TOOLS, H264_FORMAT, H264_SIZE, H264_MEMORY };

/* params: the parameter sets as an Annex B stream (00 00 00 01 + NAL unit, each); nal_size: the bytes of the length in front of each NAL unit of a sample (1..4). Parameter sets inside the samples are
 * read too. */
H264Dec *h264d_new(const uint8_t *params, int params_len, int nal_size, int *why);
void h264d_free(H264Dec *d);
/* forget the reference pictures (the next picture to decode has to be an IDR picture) */
void h264d_reset(H264Dec *d);
/* decode a sample (an access unit: NAL units with their lengths in front). 1: a picture is ready (h264d_rgb565 gives it), 0: no picture, -1: damaged / not supported (see h264d_why). */
int h264d_decode(H264Dec *d, const uint8_t *sample, int len);
int h264d_why(const H264Dec *d); /* the reason (enum above) of the last -1, or H264_OK */
int h264d_width(const H264Dec *d);  /* what is shown (the cropping of the SPS taken off) */
int h264d_height(const H264Dec *d);
/* the picture of the last decode (when it gave 1) as 16 bits per pixel in the PSP's 5650 order (red in the low bits); stride in pixels. Only the blocks that changed since the last picture that was
 * converted into the same buffer are made again, so a buffer must always be given the same `slot` (0, 1 or 2: up to three buffers) and a slot the same buffer. */
void h264d_rgb565(const H264Dec *d, uint16_t *dst, int stride, int slot);
/* the part of the picture of the last decode that is shown (when it gave 1), as planes: Y into y (rows of ys bytes), U and V into u and v (rows of cs bytes, half the width and height): for a picture whose colours
 * are made elsewhere */
void h264d_copy_planes(const H264Dec *d, unsigned char *y, int ys, unsigned char *u, unsigned char *v, int cs);
/* the number of reference frames the SPS asks for, for the log */
int h264d_nref(const H264Dec *d);
/* the loop filter is left out of the pictures that follow (1) or put back (0): about a third of the time of a picture, and what a later picture is predicted from is then not quite as it should be */
void h264d_set_fast(H264Dec *d, int fast);

#endif
