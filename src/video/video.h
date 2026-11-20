/* psp5 authentic – the video engine: opens an MP4 (H.264 + AAC) or a PMF / PSMF, decodes it on the PSP's own decoders and keeps the pictures and the sound in step.
 *
 * Both kinds go through the PSP's sceMpeg library for the picture: a PMF is read from the file as it is; an MP4 is cut into samples (mp4.c) and written as the program stream sceMpeg
 * reads (mpegps.c), a sector at a time, when the ring buffer asks for more. The sound of a PMF comes out of sceMpegAtracDecode, that of an MP4 out of sceAudiocodec (AAC); either way
 * it is put in a queue that a thread of its own plays, and what has been played is the clock the pictures follow.
 *
 * Everything runs from video_update() (once a frame) apart from that thread. */
#ifndef VIDEO_H
#define VIDEO_H

typedef struct Video Video;

typedef struct {
    int allow_software_h264;
    int force_rgb565;
    int allow_hardware_decoder;
    int allow_pmf;
    int allow_legacy_mpeg;
    int allow_yuv;
} VideoOpenOptions;

/* The only currently supported per-session policy is MP4/H.264 software RGB565. */
#define VIDEO_OPEN_OPTIONS_SAFE_RGB565 \
    { 1, 1, 0, 0, 0, 0 }

typedef enum {
    VIDEO_OPEN_OK = 0,
    VIDEO_OPEN_INVALID_ARGUMENT,
    VIDEO_OPEN_INVALID_OPTIONS,
    VIDEO_OPEN_UNSUPPORTED_CONTAINER,
    VIDEO_OPEN_UNSUPPORTED_CODEC,
    VIDEO_OPEN_UNSUPPORTED_OUTPUT,
    VIDEO_OPEN_UNSAFE_ROUTE,
    VIDEO_OPEN_OUT_OF_MEMORY,
    VIDEO_OPEN_ENGINE_ERROR,
    VIDEO_OPEN_THREAD_ERROR
} VideoOpenStatus;

/* Frame storage owned by the video engine; kept separate from the shared gfx Texture. */
typedef struct {
    int w, h;
    int tw, th;
    int stride;
    int psm;
    void *data;
    int opaque;
    int vram;
} VideoFrame;

#ifdef VIDEO_DEBUG
void vlog(const char *fmt, ...); /* a line in ms0:/PSP5/video.log (test builds) */
#endif

/* NULL when the file cannot be played (not a video the PSP can decode, damaged, bigger than 512 x 512, no memory, the PSP's decoders not there) */
Video *video_open(const char *path);
/* Opens one session with an explicit policy. Currently accepts only
 * VIDEO_OPEN_OPTIONS_SAFE_RGB565. Returns a specific failure status and leaves
 * *out_video NULL on failure. Does not read video-opts.txt. */
VideoOpenStatus video_open_ex(const char *path, const VideoOpenOptions *options, Video **out_video);
void video_close(Video *v);

int video_width(const Video *v);
int video_height(const Video *v);
float video_duration(const Video *v); /* seconds, 0 when not known */
int video_has_sound(const Video *v);

/* starts at `at` seconds (a jump: the decoders start again at the nearest place that can be started at, the pictures before `at` are not shown) */
int video_play(Video *v, float at);
void video_pause(Video *v, int paused);
/* once a frame (also while paused, to be told when a picture that was asked for has come): 1 when there is a new picture to draw */
int video_update(Video *v, float dt);
const VideoFrame *video_picture(const Video *v); /* the last picture, in the size of the video (NULL before the first, and NULL for a video whose picture is given as planes: video_yuv) */
/* The last picture of a video that is decoded in software, as planes for gfx_yuv() (the graphics chip makes the colours): Y, U, V as 8 bit rows (ystride, cstride), the size of the video; NULL when the
 * picture comes as a texture (video_picture) or there is none yet. Valid until the next call of video_update(). */
typedef struct {
    const unsigned char *plane[3];
    int w, h, ystride, cstride;
} VideoYuv;
const VideoYuv *video_yuv(const Video *v);
/* 1 when the pictures of this video are made by psp5's own decoder (on a thread of its own): the screen then needs to be drawn only every second vertical blank, the picture does not change faster than that */
int video_soft(const Video *v);
float video_time(const Video *v);              /* where the video is, in seconds */
int video_ended(const Video *v);
int video_failed(const Video *v); /* the decoder gave up: the video cannot go on */
const char *video_error(const Video *v); /* a human-readable reason when opening or playback failed; NULL otherwise */
/* 1 when the PSP's video library gave a fatal error earlier (the decoder's own, or a semaphore of its that is gone): after that every video fails, and video_open() refuses them, until psp5 is started again */
int video_poisoned(void);

#endif
