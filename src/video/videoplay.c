/* psp5 authentic – the video player (see videoplay.h) */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <psppower.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "videoplay.h"
#include "music.h"
#include "video.h"
#include "videomem.h"
#include "gfx.h"
#include "lang.h"
#include "look.h"
#include "popup.h"

#define OVERLAY_SHOW 3.0f
#define OVERLAY_FADE 0.5f
#define SEEK_STEP 10.0f      /* seconds a press of ← or → jumps */
#define SEEK_WAIT 0.4f       /* seconds after the last press before the video jumps (the bar shows where it will go) */
#define SAVE_EVERY 15.0f     /* seconds between two times the place is written down */
#define BAR_X0 84.0f
#define BAR_X1 396.0f
#define BAR_Y 221.0f

struct VideoPlayer {
    const VideoEntry *files;
    VideoEntry single;
    char *single_path, *single_name;
    char *dir;
    int count, index;
    Video *vid;
    int failed;          /* this video cannot be played */
    int poison;          /* ... because the PSP's video library failed on an earlier one: psp5 has to be started again */
    StrId error_text;
    int ask;             /* the question "continue from ..." is on screen */
    int ask_sel;         /* 0 = continue, 1 = from the beginning */
    float resume_at;
    int len_s;           /* the length in whole seconds, what the memory knows a video by */
    int audio_lease;
    int paused, ended;
    float overlay;
    float time, since_save;
    int rep_dir;
    float rep_hold;
    int seek_pending;
    float seek_target, seek_wait;
    int asleep;          /* the PSP sleeps: the video waits */
    int woke;            /* the PSP has slept while this video was open (or being opened): the decoder is not given another sample, the player leaves (see videoplay_sleep) */
    int held;            /* a popup of the launcher is over the video: it waits */
    float ask_anim;
    volatile int busy;   /* the loading thread is opening or starting a video of this player: the screen's thread leaves vid and what the thread writes alone, and shows the wheel (written by the screen's thread before a job and by the loading thread as it ends) */
    int was_busy;        /* (the screen's thread: busy was 1, and the pause and the overlay are put right when it is 0) */
};

enum { JOB_LOAD, JOB_PLAY, JOB_CLOSE, JOB_DROP };
typedef struct { int kind; VideoPlayer *v; float at; } Job;
static int submit(int kind, VideoPlayer *v, float at);
static void drop(VideoPlayer *v);
static VideoPlayer * volatile s_active_player;
static VideoPlayer * volatile s_active_session;
static StrId s_open_error = S_VID_CANT;
static volatile int s_player_slot_lock;

static void release_audio_lease(VideoPlayer *v)
{
    if (v && v->audio_lease) {
        v->audio_lease = 0;
        music_video_audio_release();
    }
}

static void say_time(char *out, int size, float sec)
{
    int s = sec < 0 ? 0 : (int)sec;
    if (s >= 3600)
        snprintf(out, size, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else
        snprintf(out, size, "%d:%02d", s / 60, s % 60);
}

static const char *current_name(const VideoPlayer *v)
{
    return v->files[v->index].name;
}

static int path_is_mp4(const char *path)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot && !strcasecmp(dot, ".mp4");
}

static char *copy_string(const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (copy)
        memcpy(copy, s, n);
    return copy;
}

static int wait_music_stopped(void)
{
    int stable_ms = 0;
    music_stop();
    for (int elapsed = 0; elapsed < 5000; elapsed += 10) {
        if (music_state() == MUSIC_STOPPED) {
            stable_ms += 10;
            if (stable_ms >= 500)
                return 1;
        } else {
            stable_ms = 0;
        }
        if (elapsed > 0 && elapsed % 100 == 0)
            music_stop(); /* retry if the music command queue dropped the earlier request */
        sceKernelDelayThread(10000);
    }
    return 0;
}

static StrId open_error_text(VideoOpenStatus status)
{
    switch (status) {
    case VIDEO_OPEN_UNSUPPORTED_CONTAINER: return S_VID_MP4_ONLY;
    case VIDEO_OPEN_UNSUPPORTED_CODEC: return S_VID_CODEC;
    case VIDEO_OPEN_UNSUPPORTED_OUTPUT:
    case VIDEO_OPEN_UNSAFE_ROUTE:
    case VIDEO_OPEN_INVALID_OPTIONS: return S_VID_OUTPUT;
    case VIDEO_OPEN_OUT_OF_MEMORY: return S_VID_MEMORY;
    case VIDEO_OPEN_THREAD_ERROR: return S_VID_THREAD;
    default: return S_VID_CANT;
    }
}

static StrId runtime_error_text(const char *error)
{
    if (error && (strstr(error, "thread") || strstr(error, "start")))
        return S_VID_THREAD;
    if (error && strstr(error, "audio"))
        return S_VID_AUDIO;
    if (error && strstr(error, "memory"))
        return S_VID_MEMORY;
    return S_VID_PLAYBACK;
}

static void fail_open(VideoPlayer *v, VideoOpenStatus status)
{
    v->error_text = open_error_text(status);
    v->poison = status == VIDEO_OPEN_ENGINE_ERROR && video_poisoned();
    v->failed = 1;
}

static void fail_runtime(VideoPlayer *v)
{
    const char *error = video_error(v->vid);
    v->error_text = runtime_error_text(error);
    v->failed = 1;
    if (v->vid) {
        v->busy = 1;
        v->was_busy = 0;
        if (!submit(JOB_DROP, v, 0)) {
            drop(v);
            release_audio_lease(v);
            v->busy = 0;
        }
    }
}

/* writes down where the video is, when it is somewhere in the middle (and forgets it when it was seen to the end) */
static void remember(VideoPlayer *v)
{
    if (!v->vid || v->failed || v->ask || v->len_s <= 0)
        return;
    if (v->ended)
        videomem_forget(current_name(v), v->len_s);
    else
        videomem_set(current_name(v), v->len_s, video_time(v->vid));
}

static void drop(VideoPlayer *v)
{
    if (v->vid)
        video_close(v->vid);
    v->vid = NULL;
    if (s_active_session == v)
        s_active_session = NULL;
}

static void free_player(VideoPlayer *v)
{
    if (!v)
        return;
    drop(v);
    while (__sync_lock_test_and_set(&s_player_slot_lock, 1))
        sceKernelDelayThread(1000);
    free(v->dir);
    free(v->single_path);
    free(v->single_name);
    release_audio_lease(v);
    if (s_active_player == v)
        s_active_player = NULL;
    __sync_lock_release(&s_player_slot_lock);
    free(v);
}

/* What opening a video costs on a PSP-1000 (the PSP-1000 timings: 2.2 s to open, 0.5 s to close the one before; the start of the decoder, the modules and the Media Engine, and a line written to the Memory
 * Stick for every step of it) was a screen that stood still. It is done by a thread of its own (`psp5_vload`), one job at a time in the order they are given; while v->busy the screen's thread only
 * shows the wheel and answers ○. The jobs: open (and start, or ask) the video of v->index, start it at a place, close a player that was left while it was loading. */
static void do_load(VideoPlayer *v)
{
    drop(v);
    v->failed = v->ask = v->ended = v->paused = v->seek_pending = v->poison = 0;
    v->error_text = S_VID_CANT;
    v->since_save = 0;
    v->overlay = OVERLAY_SHOW;
    if (!v->audio_lease) {
        if (!music_video_audio_acquire()) {
            v->error_text = S_VID_AUDIO;
            v->failed = 1;
            return;
        }
        v->audio_lease = 1;
    }
    if (!wait_music_stopped()) {
        v->error_text = S_VID_AUDIO;
        v->failed = 1;
        release_audio_lease(v);
        return;
    }
    const VideoEntry *entry = &v->files[v->index];
    const char *entry_path = entry->path;
    size_t path_len;
    if (entry_path) {
        path_len = strlen(entry_path);
    } else {
        size_t dir_len = strlen(v->dir), name_len = strlen(entry->name);
        if (dir_len > (size_t)-1 - name_len - 2) {
            v->error_text = S_VID_MEMORY;
            v->failed = 1;
            release_audio_lease(v);
            return;
        }
        path_len = dir_len + 1 + name_len;
    }
    if (path_len == (size_t)-1) {
        v->error_text = S_VID_MEMORY;
        v->failed = 1;
        release_audio_lease(v);
        return;
    }
    char *path = malloc(path_len + 1);
    if (!path) {
        v->error_text = S_VID_MEMORY;
        v->failed = 1;
        release_audio_lease(v);
        return;
    }
    if (entry_path)
        memcpy(path, entry_path, path_len + 1);
    else
        snprintf(path, path_len + 1, "%s/%s", v->dir, entry->name);
    if (!path_is_mp4(path)) {
        free(path);
        fail_open(v, VIDEO_OPEN_UNSUPPORTED_CONTAINER);
        release_audio_lease(v);
        return;
    }
    VideoOpenOptions options = VIDEO_OPEN_OPTIONS_SAFE_RGB565;
    if (s_active_session && s_active_session != v) {
        free(path);
        v->error_text = S_VID_BUSY;
        v->failed = 1;
        release_audio_lease(v);
        return;
    }
    VideoOpenStatus status = video_open_ex(path, &options, &v->vid);
    if (status != VIDEO_OPEN_OK || !v->vid) {
#ifdef VIDEO_DEBUG
        vlog("open failed (%d): %s\n", status, path);
#endif
        free(path);
        fail_open(v, status);
        release_audio_lease(v);
        return;
    }
    free(path);
    s_active_session = v;
    if (!video_has_sound(v->vid)) {
        v->error_text = S_VID_AUDIO;
        v->failed = 1;
        drop(v);
        release_audio_lease(v);
        return;
    }
    v->len_s = (int)(video_duration(v->vid) + 0.5f);
    float at = 0;
    if (v->len_s > 0 && videomem_get(current_name(v), v->len_s, &at) && at > 0) { /* it was left in the middle: ask */
        v->ask = 1;
        v->ask_sel = 0;
        v->resume_at = at;
        return;
    }
    if (!video_play(v->vid, 0)) {
        v->error_text = runtime_error_text(video_error(v->vid));
        v->failed = 1;
        drop(v);
        release_audio_lease(v);
    }
}

static void do_play(VideoPlayer *v, float at)
{
    if (!v->vid || !video_play(v->vid, at)) {
        v->error_text = runtime_error_text(video_error(v->vid));
        v->failed = 1;
        drop(v);
        release_audio_lease(v);
    }
}

static SceUID s_load_thread = -1;
static volatile int s_load_quit;
static Job s_jobs[8];
static volatile unsigned s_jobs_head, s_jobs_tail; /* head: the screen's thread writes it, tail: the loading thread (no atomic operation: see files.c) */

static int load_thread(SceSize args, void *argp)
{
    while (!s_load_quit) {
        while (s_jobs_tail != s_jobs_head) {
            Job j = s_jobs[s_jobs_tail % 8];
            VideoPlayer *v = j.v;
            if (j.kind == JOB_LOAD) {
                do_load(v);
            } else if (j.kind == JOB_PLAY) {
                do_play(v, j.at);
            } else if (j.kind == JOB_DROP) {
                drop(v);
                release_audio_lease(v);
            } else { /* the player was left while it was loading: it is the thread's now */
                free_player(v);
                v = NULL;
            }
            __sync_synchronize();
            if (v)
                v->busy = 0; /* (the last touch: the screen's thread may free the player after this) */
            s_jobs_tail++;
        }
        if (s_load_quit)
            break;
        sceKernelSleepThread(); /* (submit wakes it: a wake-up before the sleep is counted by the PSP and not lost) */
    }
    return 0;
}

/* the screen's thread: gives the thread a job; 0 when it cannot (the job is then done here, as it was before the thread) */
static int submit(int kind, VideoPlayer *v, float at)
{
    if (s_load_thread < 0) {
        s_load_quit = 0;
        SceUID th = sceKernelCreateThread("psp5_vload", load_thread, 0x30, 0x10000, PSP_THREAD_ATTR_USER, NULL);
        if (th >= 0 && sceKernelStartThread(th, 0, NULL) >= 0)
            s_load_thread = th;
        else {
            if (th >= 0)
                sceKernelDeleteThread(th);
            return 0;
        }
    }
    unsigned h = s_jobs_head;
    if (h - s_jobs_tail >= 8)
        return 0;
    s_jobs[h % 8].kind = kind;
    s_jobs[h % 8].v = v;
    s_jobs[h % 8].at = at;
    __sync_synchronize();
    s_jobs_head = h + 1;
    sceKernelWakeupThread(s_load_thread);
    return 1;
}

void videoplay_flush(void)
{
    while (s_jobs_tail != s_jobs_head)
        sceKernelDelayThread(2000);
    if (s_load_thread >= 0) {
        s_load_quit = 1;
        __sync_synchronize();
        sceKernelWakeupThread(s_load_thread);
        sceKernelWaitThreadEnd(s_load_thread, NULL);
        sceKernelDeleteThread(s_load_thread);
        s_load_thread = -1;
        s_load_quit = 0;
    }
}

/* opens (and starts, or asks about) the video of v->index: the thread does it */
static void start_load(VideoPlayer *v)
{
    v->busy = 1;
    v->was_busy = 1;
    if (!submit(JOB_LOAD, v, 0)) {
        do_load(v);
        v->busy = 0;
    }
}

static void start_play(VideoPlayer *v, float at)
{
    v->busy = 1;
    v->was_busy = 1;
    if (!submit(JOB_PLAY, v, at)) {
        do_play(v, at);
        v->busy = 0;
    }
}

static void begin_player(VideoPlayer *v)
{
    if (!music_video_audio_acquire()) {
        v->error_text = S_VID_AUDIO;
        v->failed = 1;
        return;
    }
    v->audio_lease = 1;
    music_stop(); /* the video may use the shared PSP audio output */
    start_load(v);
}

static int reserve_player_slot(void)
{
    while (__sync_lock_test_and_set(&s_player_slot_lock, 1))
        sceKernelDelayThread(1000);
    if (s_active_player) {
        s_open_error = S_VID_BUSY;
        __sync_lock_release(&s_player_slot_lock);
        return 0;
    }
    return 1;
}

static VideoPlayer *open_player(const char *dir, const VideoEntry *files, int count, int index, int require_paths)
{
    if (!reserve_player_slot())
        return NULL;
    if (!files || count <= 0 || index < 0 || index >= count) {
        s_open_error = S_VID_CANT;
        __sync_lock_release(&s_player_slot_lock);
        return NULL;
    }
    for (int i = 0; i < count; i++)
        if (!files[i].name || !files[i].name[0] || (require_paths && (!files[i].path || !files[i].path[0]))) {
            s_open_error = S_VID_CANT;
            __sync_lock_release(&s_player_slot_lock);
            return NULL;
        }
    VideoPlayer *v = calloc(1, sizeof(*v));
    if (!v) {
        s_open_error = S_VID_MEMORY;
        __sync_lock_release(&s_player_slot_lock);
        return NULL;
    }
    v->dir = copy_string(dir ? dir : "");
    if (!v->dir) {
        free(v);
        s_open_error = S_VID_MEMORY;
        __sync_lock_release(&s_player_slot_lock);
        return NULL;
    }
    v->files = files;
    v->count = count;
    v->index = index;
    s_active_player = v;
    s_open_error = S_VID_CANT;
    __sync_lock_release(&s_player_slot_lock);
    begin_player(v);
    return v;
}

VideoPlayer *videoplay_open_file(const char *path, const char *name)
{
    if (!reserve_player_slot())
        return NULL;
    if (!path || !path[0]) {
        s_open_error = S_VID_CANT;
        __sync_lock_release(&s_player_slot_lock);
        return NULL;
    }
    VideoPlayer *v = calloc(1, sizeof(*v));
    if (!v) {
        s_open_error = S_VID_MEMORY;
        __sync_lock_release(&s_player_slot_lock);
        return NULL;
    }
    v->single_path = copy_string(path);
    if (!name || !name[0]) {
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        v->single_name = copy_string(base);
    } else {
        v->single_name = copy_string(name);
    }
    if (!v->single_path || !v->single_name) {
        free(v->single_path);
        free(v->single_name);
        free(v);
        s_open_error = S_VID_MEMORY;
        __sync_lock_release(&s_player_slot_lock);
        return NULL;
    }
    v->single.name = v->single_name;
    v->single.path = v->single_path;
    v->files = &v->single;
    v->count = 1;
    s_active_player = v;
    s_open_error = S_VID_CANT;
    __sync_lock_release(&s_player_slot_lock);
    begin_player(v);
    return v;
}

VideoPlayer *videoplay_open(const char *dir, const VideoEntry *files, int count, int index)
{
    if (!dir || !dir[0]) {
        s_open_error = S_VID_CANT;
        return NULL;
    }
    return open_player(dir, files, count, index, 0);
}

VideoPlayer *videoplay_open_list(const VideoEntry *files, int count, int index)
{
    return open_player("", files, count, index, 1);
}

const char *videoplay_open_error(void)
{
    return L(s_open_error);
}

void videoplay_close(VideoPlayer *v)
{
    if (!v)
        return;
    if (!gfx_video_frame_update_ready())
        gfx_video_frame_quiesce(); /* retire the GU list before video_close() can free its frame buffers */
    if (!v->busy)
        remember(v);
    /* The thread closes the video, in its own time: closing takes 0.5 s on a PSP-1000 (the PSP-1000 timings) and the screen does not wait for it; a player that is still being opened is closed when the thread
     * is done with it (the jobs are done in the order they were given). The player is the thread's from here: ui_free() and files_close() wait for it (videoplay_flush) before psp5 is let go. */
    if (submit(JOB_CLOSE, v, 0))
        return;
    while (v->busy) /* (no thread to be had: here, as it was) */
        sceKernelDelayThread(2000);
    free_player(v);
}

/* the engine waits when the user paused it, the question is on screen, a popup of the launcher is over it or the PSP sleeps */
static void apply_pause(VideoPlayer *v)
{
    if (v->vid && !v->busy)
        video_pause(v->vid, v->paused || v->ask || v->held || v->asleep || v->woke);
}

void videoplay_sleep(VideoPlayer *v, int asleep)
{
    v->asleep = asleep;
    apply_pause(v);
}

/* A PSP that has slept does not give a file back that was open before the sleep (the reads fail: the log of 2026-11-15 had hw-error -9 on the first sample after the wake, and the way on from there, the
 * games' library started on the same file, crashed the PSP), and the Media Engine has been reset. So the video is not taken up again: the decoder stays paused from the wake on, the player leaves at its
 * next update as if the circle button had been pressed, and the place is remembered like at every leaving: opened again it asks "Continue from". The callers say it (main.c counts the wake-ups of the
 * PSP's power callback: it does not depend on a frame having seen the PSP go to sleep). */
void videoplay_woke(VideoPlayer *v)
{
    v->woke = 1;
    apply_pause(v);
}

void videoplay_hold(VideoPlayer *v, int hold)
{
    if (hold == v->held)
        return;
    v->held = hold;
    apply_pause(v);
}

static void go(VideoPlayer *v, int by)
{
    if (v->count < 2)
        return;
    remember(v);
    v->index = (v->index + by + v->count) % v->count;
    start_load(v);
}

static void nudge(VideoPlayer *v, int dir)
{
    float now = v->seek_pending ? v->seek_target : video_time(v->vid);
    float dur = video_duration(v->vid);
    now += dir * SEEK_STEP;
    if (now < 0)
        now = 0;
    if (dur > 1 && now > dur - 1)
        now = dur - 1;
    v->seek_target = now;
    v->seek_pending = 1;
    v->seek_wait = SEEK_WAIT;
}

int videoplay_update(VideoPlayer *v, u32 pressed, u32 held, int lx, int ly, float dt)
{
    /* Do not let the decoder recycle its front buffer until gfx_end() has finished the list. */
    if (!gfx_video_frame_update_ready())
        return 0;
    if (v->busy) { /* the thread is opening or starting a video: the wheel turns, ○ leaves, nothing else is touched */
        v->time += dt;
        return (pressed & PSP_CTRL_CIRCLE) || v->woke ? 1 : 0;
    }
    if (v->woke) /* (the PSP slept: see videoplay_sleep) */
        return 1;
    if (v->was_busy) { /* it is ready (or has failed) */
        v->was_busy = 0;
        v->overlay = OVERLAY_SHOW;
        apply_pause(v);
    }
    /* A video that is decoded in software needs all the processor there is: the loop that draws the screen (about two milliseconds a turn) runs at half the speed, a picture is drawn every second vertical blank. */
    if (v->vid && !v->failed && video_soft(v->vid))
        sceDisplayWaitVblankStart();
    v->time += dt;
    if (v->overlay > 0)
        v->overlay -= dt;
    v->ask_anim += ((v->ask ? 1.0f : 0.0f) - v->ask_anim) * fminf(1, dt * 12);
    if (pressed)
        v->overlay = OVERLAY_SHOW;
    if (pressed & PSP_CTRL_CIRCLE)
        return 1;
    if (pressed & PSP_CTRL_LTRIGGER) {
        go(v, -1);
        return 0; /* do not touch the old session after its queued replacement may begin */
    } else if (pressed & PSP_CTRL_RTRIGGER) {
        go(v, 1);
        return 0;
    }
    if (v->failed || !v->vid)
        return 0;
    if (v->ask) { /* the question: continue where it was left, or from the beginning */
        if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN))
            v->ask_sel = !v->ask_sel;
        if (pressed & PSP_CTRL_CROSS) {
            if (v->ask_sel == 1)
                videomem_forget(current_name(v), v->len_s); /* from the beginning: the memory of the place is gone */
            v->ask = 0;
            start_play(v, v->ask_sel == 0 ? v->resume_at : 0);
        }
        return 0;
    }
    if (pressed & PSP_CTRL_CROSS) {
        if (v->ended) { /* played to the end: ✕ plays it again */
            v->ended = 0;
            v->paused = 0;
            apply_pause(v);
            if (!video_play(v->vid, 0)) {
                fail_runtime(v);
                return 0;
            }
        } else {
            v->paused = !v->paused;
            apply_pause(v);
        }
    }
    int dir = (held & PSP_CTRL_RIGHT) ? 1 : (held & PSP_CTRL_LEFT) ? -1 : 0;
    if (dir != v->rep_dir) {
        v->rep_dir = dir;
        v->rep_hold = 0;
        if (dir)
            nudge(v, dir);
    } else if (dir) {
        v->rep_hold += dt;
        if (v->rep_hold > 0.4f) {
            v->rep_hold -= 0.15f;
            nudge(v, dir);
        }
    }
    if (dir)
        v->overlay = OVERLAY_SHOW;
    if (v->seek_pending && !dir) {
        v->seek_wait -= dt;
        if (v->seek_wait <= 0) {
            v->seek_pending = 0;
            v->ended = 0;
            if (!video_play(v->vid, v->seek_target)) {
#ifdef VIDEO_DEBUG
                vlog("the jump to %.2f failed\n", v->seek_target);
#endif
                fail_runtime(v);
                return 0;
            }
            apply_pause(v);
        }
    }
    if (!v->paused && !v->ask)
        scePowerTick(PSP_POWER_TICK_ALL); /* no button is pressed while a film is watched: the PSP must not dim the screen or go to sleep meanwhile */
    video_update(v->vid, dt);
    if (video_failed(v->vid)) {
#ifdef VIDEO_DEBUG
        vlog("the engine gave up at %.2f\n", video_time(v->vid));
#endif
        fail_runtime(v);
        return 0;
    }
    /* Validate only a short-lived view; never retain decoder storage past this update. */
    const VideoFrame *frame = video_picture(v->vid);
    if (frame && (!frame->data || frame->psm != GU_PSM_5650 || frame->stride < frame->w)) {
        v->error_text = S_VID_OUTPUT;
        v->failed = 1;
        v->busy = 1;
        if (!submit(JOB_DROP, v, 0)) {
            drop(v);
            release_audio_lease(v);
            v->busy = 0;
        }
        return 0;
    }
    if (video_ended(v->vid) && !v->ended) {
        v->ended = 1;
        v->overlay = OVERLAY_SHOW;
        videomem_forget(current_name(v), v->len_s);
    }
    v->since_save += dt;
    if (v->since_save >= SAVE_EVERY && !v->paused && !v->ended) {
        v->since_save = 0;
        remember(v);
    }
    return 0;
}

/* a key sign of the overlay: a light pill with a dark letter */
static void key_sign(float cx, float cy, const char *label, float a)
{
    gfx_round_rect(cx - 13, cy - 8, 26, 16, 5, RGBA(190, 192, 200, (int)(210 * a)));
    text_draw(cx, cy + 4, 0.5f, 1, RGBA(28, 28, 34, (int)(255 * a)), ALIGN_CENTER, label);
}

static void draw_video_frame(VideoPlayer *v)
{
    const VideoFrame *frame = video_picture(v->vid);
    if (!frame || !frame->data || frame->psm != GU_PSM_5650)
        return;
    float k = fminf((float)SCREEN_W / frame->w, (float)SCREEN_H / frame->h);
    float w = floorf(frame->w * k + 0.5f), h = floorf(frame->h * k + 0.5f);
    if (video_failed(v->vid))
        return;
    gfx_video_frame(frame->data, frame->w, frame->h, frame->stride, frame->psm, frame->opaque,
                    floorf((SCREEN_W - w) / 2), floorf((SCREEN_H - h) / 2), w, h);
}

static void draw_overlay(const VideoPlayer *v, float a)
{
    gfx_gradient_v(0, 0, SCREEN_W, 56, RGBA(0, 0, 0, (int)(150 * a)), RGBA(0, 0, 0, 0));
    gfx_gradient_v(0, SCREEN_H - 100, SCREEN_W, 100, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, (int)(170 * a)));
    char n[24];
    snprintf(n, sizeof(n), "%d/%d", v->index + 1, v->count);
    text_draw(SCREEN_W / 2, 31, 0.8f, 0, RGBA(238, 238, 242, (int)(255 * a)), ALIGN_CENTER, n);
    if (v->count > 1) {
        key_sign(34, 26, "L", a);
        key_sign(SCREEN_W - 34, 26, "R", a);
    }
    if (!v->vid || v->failed)
        return;
    float dur = video_duration(v->vid), now = v->seek_pending ? v->seek_target : video_time(v->vid);
    float share = dur > 0 ? fminf(1, fmaxf(0, now / dur)) : 0, bw = BAR_X1 - BAR_X0;
    char t[16];
    say_time(t, sizeof(t), now);
    text_draw(32, BAR_Y + 5, 0.62f, 0, RGBA(238, 238, 242, (int)(255 * a)), ALIGN_LEFT, t);
    if (dur > 0) {
        say_time(t, sizeof(t), dur);
        text_draw(SCREEN_W - 32, BAR_Y + 5, 0.62f, 0, RGBA(238, 238, 242, (int)(255 * a)), ALIGN_RIGHT, t);
    }
    look_bar(BAR_X0, BAR_Y - 1.5f, bw, 3, bw * share, a);
    float kx = BAR_X0 + bw * share;
    gfx_circle(kx, BAR_Y, 13, RGBA(236, 236, 240, (int)(255 * a)));
    u32 dark = RGBA(20, 20, 26, (int)(255 * a));
    if (v->paused || v->ended) {
        const float tri[] = { kx - 3.5f, BAR_Y - 5.5f, kx - 3.5f, BAR_Y + 5.5f, kx + 5.5f, BAR_Y };
        gfx_fan(kx, BAR_Y, tri, 3, dark);
    } else {
        gfx_rect(kx - 4, BAR_Y - 5, 3, 10, dark);
        gfx_rect(kx + 1, BAR_Y - 5, 3, 10, dark);
    }
}

static void draw_question(const VideoPlayer *v, float a)
{
    char first[64], t[16];
    say_time(t, sizeof(t), v->resume_at);
    snprintf(first, sizeof(first), L(S_VID_RESUME), t);
    const char *rows[2] = { first, L(S_VID_RESTART) };
    float w = 200;
    for (int k = 0; k < 2; k++)
        w = fmaxf(w, ceilf(text_width(0.62f, 0, rows[k])) + 32);
    w = 2 * ceilf(fminf(w, SCREEN_W - 40) / 2);
    float h = 2 * POPUP_EDGE + 2 * POPUP_ITEM, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2 + (1 - a) * 10;
    popup_dim(a);
    popup_panel(x, y, w, h, POPUP_DARK, a);
    for (int k = 0; k < 2; k++) {
        float ry = y + POPUP_EDGE + k * POPUP_ITEM;
        u32 col = POPUP_TEXT;
        if (k == v->ask_sel)
            popup_choice(x + POPUP_EDGE, ry, w - 2 * POPUP_EDGE, POPUP_ITEM, a);
        else
            col = COLOR_ALPHA(col, 204);
        text_draw(x + 16, ry + 18, 0.62f, 0, COLOR_ALPHA(col, (int)(((col >> 24) & 0xFF) * a)), ALIGN_LEFT, rows[k]);
    }
}

void videoplay_draw(VideoPlayer *v)
{
    if (v->busy) { /* (vid is the thread's while it works) */
        look_wheel(SCREEN_W / 2, SCREEN_H / 2, v->time, 1);
        char n[24];
        snprintf(n, sizeof(n), "%d/%d", v->index + 1, v->count);
        text_draw(SCREEN_W / 2, 31, 0.8f, 0, RGBA(238, 238, 242, 255), ALIGN_CENTER, n);
        return;
    }
    if (v->vid && video_failed(v->vid)) {
        fail_runtime(v);
        return;
    }
    if (v->vid && !v->failed) {
        draw_video_frame(v);
    }
    if (v->failed)
        text_draw(SCREEN_W / 2, SCREEN_H / 2 + 5, 0.7f, 0, RGBA(238, 238, 242, 255), ALIGN_CENTER,
                  L(v->poison ? S_VID_POISON : v->error_text));
    float a = v->overlay >= OVERLAY_FADE ? 1 : v->overlay > 0 ? v->overlay / OVERLAY_FADE : 0;
    if (v->failed || v->ask)
        a = fmaxf(a, v->failed ? 1 : 0);
    if (a > 0)
        draw_overlay(v, a);
    if (v->ask_anim > 0.02f)
        draw_question(v, v->ask_anim);
}
