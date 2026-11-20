/* psp5 authentic – an AAC-LC decoder in software (ISO/IEC 14496-3), for the sound of an MP4.
 *
 * The PSP's own AAC decoder (sceAudiocodec) crashed a real PSP on its first frame, and what its parameter block wants cannot be found out without crashing more of them, so the sound of an MP4
 * is made here: raw AAC frames (as an MP4 stores them) with the AudioSpecificConfig of the file in, 16-bit stereo out. Low complexity profile, 1024 samples a frame, the tools of ordinary
 * encoders (short blocks, M/S and intensity stereo, noise substitution, TNS, pulse data). A sound of more than two channels (channel configurations 3 to 7: 3, 4, 5, 5.1 and 7.1) is mixed
 * down to two (downmix.c). Not supported (the call returns NULL, the video plays without sound): other profiles, HE-AAC (SBR), a channel layout that is in the stream (configuration 0),
 * 960-sample frames, coupling channels. */
#ifndef AAC_H
#define AAC_H

typedef struct AacDec AacDec;

#define AAC_FRAME 1024 /* the samples a frame gives (per channel) */
#define AAC_MAX_CH 8   /* the channels of the largest configuration (7.1) */

/* the decoder for a stream whose AudioSpecificConfig is cfg; NULL when it is not one this decoder can do. *rate and *channels are what the config says (the channels the stream has: 6 for 5.1 ...; the output of aac_decode is two). */
AacDec *aac_new(const unsigned char *cfg, int cfg_len, int *rate, int *channels);
void aac_free(AacDec *d);
/* the line of aac.c where the last frame that was not decoded was found damaged or not supported (0: none yet): for the log */
int aac_error_line(void);
/* forgets the overlap of the frame before (after a jump in the stream) */
void aac_flush(AacDec *d);

/* One frame: pcm gets AAC_FRAME stereo frames (interleaved 16 bits; a mono stream is made two equal channels, a stream of more channels is mixed down to two). 1 when the frame was decoded, 0 when it was damaged (pcm is then silent or what
 * could be made of it), so the sound goes on with the next one. */
int aac_decode(AacDec *d, const unsigned char *frame, int len, short *pcm);

#endif
