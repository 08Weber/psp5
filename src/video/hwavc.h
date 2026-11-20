/* psp5 authentic – the PSP's own H.264 decoder (on the Media Engine) for a CAVLC stream: the way PMPlayer Ultraviolet (cooleyes' player, GPL) reaches it. None of that player's code is here, only what
 * can be read in it about the order and the structures of the calls.
 *
 * The decoder of the library sceMpeg that games load (mpeg.prx) does not take what this route needs: PMPlayer Ultraviolet loads the VSH's library (flash0:/kd/mpeg_vsh.prx) beside the AV codec
 * modules, starts the Media Engine with its Baseline image (meboot.h), opens the library in its mode 4 with 2 MB of the Engine's memory (the first 64 KB are the library's, the access unit lives at
 * +0x10000), and gives it the samples of the MP4 as they are in the file with sceMpegGetAvcNalAu; sceMpegAvcDecode, sceMpegAvcDecodeDetail2 and sceMpegBaseCscAvc then give the picture as RGB.
 * Every step writes a line to the log (hwavc_log): the first run on a PSP-1000 of a version that used the game's library stopped at sceMpegCreate (0x807f00ff).
 *
 * Only for a stream the PSP's decoder may take: in its Baseline mode a CAVLC stream without B pictures (unit.c hw_ok), in its Main mode a Main profile stream with CABAC or B pictures (hw_main); the Engine is
 * started with the image that fits (meboot.h) before hwavc_open(). video.c goes on with the software decoder (CAVLC) or the games' library (CABAC) when any step fails. */
#ifndef HWAVC_H
#define HWAVC_H

typedef struct HwAvc HwAvc;

/* where the log lines go (tag, four numbers); NULL: nowhere */
void hwavc_log(void (*fn)(const char *tag, unsigned a, unsigned b, unsigned c, unsigned d));

/* The library that the route needs: the AV codec modules and flash0:/kd/mpeg_vsh.prx loaded and started. 0 when it is up (or was), else a negative number of the step that failed (-1 AV codec,
 * -2 loading the module, -3 starting it). hwlib_down() undoes only what hwlib_up() did. */
int hwlib_up(void);
void hwlib_down(void);
int hwlib_is_up(void);

/* Opens a decoder (the library must be up and the Engine started with the image that fits the stream): sps and pps are the NAL units as the file has them (header byte first, no start code), nal_prefix the
 * size of the length in front of each NAL unit of a sample (1 to 4). NULL when a step failed (the log says which). */
HwAvc *hwavc_open(const unsigned char *sps, int sps_len, const unsigned char *pps, int pps_len, int nal_prefix);
void hwavc_close(HwAvc *h);
/* A fresh library state (Delete, Finish, Init, Create, InitAu again) for a new place of the video: PMPlayer Ultraviolet does it at every seek. 0 when it is open, else -1 and nothing is left open. */
int hwavc_reset(HwAvc *h);

#define HWAVC_PICS 3    /* the pictures of one call that can be converted */
#define HWAVC_MAX_OUT 8 /* more than this in one call is not a decoder that works: refused */

/* One sample of the file (in memory aligned to 64 bytes, room for its size rounded up to 64): mode 3 for the first sample and for a key frame, else 0. The decoder is some samples behind and gives its
 * pictures in display order: *pics is how many came out of this call (0, 1, 2 or 3: three come out together when a key frame makes the decoder put out the pictures it held; up to HWAVC_MAX_OUT are
 * accepted). Picture k (the first HWAVC_PICS of them) is written as RGBA (8 bits each, GU_PSM_8888) with 512 pixels a row to rgb[k] (aligned to 64 bytes, room for 512 x the height rounded up to 16) when
 * that is not NULL (rgb itself may be NULL: nothing is converted, the pictures are only counted; a picture after the first HWAVC_PICS is only counted); *w and *h are the size of the first. 0 when it is done, a negative number of
 * the step that failed otherwise (-1 sceMpegGetAvcNalAu, -2 sceMpegAvcDecode, -3 sceMpegAvcDecodeDetail2 or a picture it cannot take, -4 sceMpegBaseCscAvc); *err is the library's answer. */
int hwavc_decode(HwAvc *h, void *sample, int size, int mode, void *rgb[HWAVC_PICS], int *pics, int *w, int *h_out, unsigned *err);

#endif
