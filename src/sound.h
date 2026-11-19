/* psp5 – made by 08Weber */
#ifndef SOUND_H
#define SOUND_H

/* UI sound effects – Deck-UI_Sound pack (4-bit IMA ADPCM, decoded on-the-fly with zero heap). */
typedef enum {
    SND_MOVE,       /* list / menu navigation */
    SND_TILE,       /* horizontal card scroll */
    SND_BUMPER,     /* wrap / end-of-row bumper */
    SND_SWIPE,      /* tab transition */
    SND_SELECT,     /* confirm / positive */
    SND_BACK,       /* back / cancel */
    SND_TOGGLE,     /* switch toggle on / mark */
    SND_TOGGLE_OFF, /* switch toggle off / unmark */
    SND_SLIDER_UP,  /* slider / choice increment */
    SND_SLIDER_DN,  /* slider / choice decrement */
    SND_OPEN,       /* show modal / options menu */
    SND_CLOSE,      /* hide modal / close menu */
    SND_SIDE_IN,    /* open Control Center / Settings */
    SND_SIDE_OUT,   /* close Control Center / Settings */
    SND_DETAIL_IN,  /* enter Game Info / subpage */
    SND_DETAIL_OUT, /* exit Game Info / subpage */
    SND_TYPE,       /* search keyboard typing */
    SND_TOAST,      /* toast / screenshot / saved */
    SND_MSG_TOAST,  /* confirmation / notice toast */
    SND_VOLUME,     /* hardware volume bar change */
    SND_MUSIC_ACT,  /* music play/pause/skip action */
    SND_LAUNCH,     /* launch game / app */
    SND_ERROR,      /* error / negative */
    SND_BOOT,       /* boot / wake startup sound */
    SND_COUNT
} SoundId;

/* Starts the audio mixer thread and reserves a hardware audio channel. */
void sound_init(void);
/* Stops the mixer thread and releases the audio channel. */
void sound_term(void);
void sound_play(SoundId id);
/* Waits (up to timeout_ms) until no sounds are playing. */
void sound_wait(int timeout_ms);
/* Returns 1 when sound playback is ready. */
int sound_ready(void);
/* Kept for API compatibility (boot sound is stored in rodata with zero heap). */
void sound_prepare_boot(void);
/* Skips boot sound preparation (safe start without boot screen). */
void sound_skip_boot(void);
/* Kept for API compatibility (zero heap to release). */
void sound_release(SoundId id);
/* Quickly fades out all active voices (e.g. when skipping the boot screen). */
void sound_fade_out(void);

/* On/off, saved in ms0:/PSP5/psp5.cfg (see config.c) */
extern unsigned char g_sound_on;

#ifdef PSP5_TEST
/* Test build: dump decoded sounds as WAV files for inspection */
void sound_dump(const char *dir);
#endif

#endif
