/* psp5 authentic – the video player: a video of a folder on the whole screen (MP4 with H.264 video and AAC sound, and PMF / PSMF), the bar to move in it, L / R for the video before
 * and after, ○ leaves; a video that was left in the middle asks whether to go on from there (videomem.h). The overlay (L1, the number of the video, R1 at the top; the time, the bar and the
 * length at the bottom) shows when a video starts or a button is pressed and goes away after a moment. */
#ifndef VIDEOPLAY_H
#define VIDEOPLAY_H

#include <psptypes.h>

typedef struct VideoPlayer VideoPlayer;

/* The array, entries, names and paths are borrowed and must remain valid until videoplay_close();
 * path may be NULL when dir is used. The single-file opener owns its copied strings. */
typedef struct {
    const char *name;
    const char *path;
} VideoEntry;

/* Open one path, or a folder's entries. Entry names and paths stay valid until close. */
VideoPlayer *videoplay_open_file(const char *path, const char *name);
VideoPlayer *videoplay_open(const char *dir, const VideoEntry *files, int count, int index);
/* Entries with full paths, from any folder. L / R select adjacent entries. */
VideoPlayer *videoplay_open_list(const VideoEntry *files, int count, int index);
/* Localized reason when an open function returns NULL (including an active-player conflict). */
const char *videoplay_open_error(void);
/* once a frame, with the buttons; returns 1 when the player is to be closed (○) */
int videoplay_update(VideoPlayer *v, u32 pressed, u32 held, int lx, int ly, float dt);
/* draws the player on the whole screen (no gfx_begin / gfx_end) */
void videoplay_draw(VideoPlayer *v);
void videoplay_close(VideoPlayer *v);
/* the PSP goes to sleep / wakes up: the video is paused (and goes on when it wakes if it played) */
void videoplay_sleep(VideoPlayer *v, int asleep);
void videoplay_woke(VideoPlayer *v); /* the PSP has slept and woken while the player was open: it stays paused and leaves at its next update */
/* waits until the loading thread has done what it was given (before psp5 is let go: a game that starts, the way out to the XMB, the end of the File Manager) */
void videoplay_flush(void);
/* a popup of the launcher is over the video (the power popup): the video waits, and goes on when it is gone */
void videoplay_hold(VideoPlayer *v, int hold);

#endif
