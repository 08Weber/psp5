/* psp5 authentic – a sound with more than two channels (5.1, 7.1 ...) mixed down to the two channels the PSP's output has. Shared by the AAC decoder (aac.c: the sound of an MP4, of an .m4a, of a
 * raw .aac) and the WAV files of the music player, so both players hear the same mix.
 *
 * The mix is the usual one (ITU-R BS.775: what ffmpeg and most players do, the volume apart): the front left and right as they are, the centre into both at -3 dB, a surround pair into its
 * side at -3 dB (when a file has two surround pairs, side and back, each at -6 dB), a rear centre into both at -6 dB; the low-frequency channel is left out (as is usual: it would only make the
 * bass boomy through two small speakers). All of it is scaled by 1 / (1 + 0.7071), so a loud front pair with the centre on top does not clip; a mono or a stereo sound is not touched. */
#ifndef DOWNMIX_H
#define DOWNMIX_H

enum { /* the place of a channel */
    DM_NONE, /* not mixed (a channel above the listener, ...) */
    DM_FL, DM_FR, DM_FC, DM_LFE,
    DM_BL, DM_BR,   /* surround at the back (AAC's "surround" pair is this) */
    DM_SL, DM_SR,   /* at the sides */
    DM_BC,          /* rear centre */
    DM_FLC, DM_FRC  /* front, left and right of the centre */
};

#define DM_MAX 8        /* the most channels any of the players takes */
#define DM_GAIN 0.58578644f /* 1 / (1 + 0.70710678) */

/* How much of each of the n channels (places in role[]) goes to the left (wl[]) and to the right (wr[]) output. */
void dm_weights(const unsigned char *role, int n, float *wl, float *wr);

/* The places of the channels of a WAV file (the order of the channels in the file) from its channel mask (WAVE_FORMAT_EXTENSIBLE; bit 0 front left, 1 front right, 2 front centre, 3 low
 * frequency, 4 back left, 5 back right, 6 and 7 front left / right of centre, 8 back centre, 9 and 10 side left / right, 11 and up above), 0 when the mask does not have n channels. */
int dm_roles_mask(unsigned mask, int n, unsigned char *role);
/* The places of n channels of a file that does not say (the layouts Windows and ffmpeg take): 3 front, 4 quad, 5.0, 5.1, 6.1, 7.1; 0 for any other n */
int dm_roles_default(int n, unsigned char *role);

#endif
