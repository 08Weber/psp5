/* psp5 – made by Mille Weber */
#ifndef SOUND_H
#define SOUND_H

/* Egna gränssnittsljud – räknas fram vid start, inga ljudfiler. */
typedef enum {
    SND_MOVE,    /* bläddra */
    SND_SWIPE,   /* byta flik */
    SND_SELECT,  /* välja / gå in */
    SND_BACK,    /* tillbaka */
    SND_TOGGLE,  /* reglage */
    SND_OPEN,    /* öppna inställningar */
    SND_LAUNCH,  /* starta spel */
    SND_ERROR,   /* något gick inte */
    SND_BOOT,    /* startskärmen */
    SND_COUNT
} SoundId;

/* Skapar ljuden och startar mixertråden. */
void sound_init(void);
/* Stoppar tråden, släpper ljudkanalen och frigör minnet. */
void sound_term(void);
void sound_play(SoundId id);
/* Väntar (högst timeout_ms) tills inga ljud spelas. */
void sound_wait(int timeout_ms);
/* 1 när ljuden är framräknade (görs i bakgrunden efter sound_init) */
int sound_ready(void);
/* Frigör ett ljud som inte behövs mer (startljudet efter startskärmen). */
void sound_release(SoundId id);
/* Tonar snabbt ut allt som spelas (t.ex. när startskärmen hoppas över). */
void sound_fade_out(void);

/* På/av, sparas i ms0:/PSP5/psp5.cfg (se config.c) */
extern unsigned char g_sound_on;

#ifdef PSP5_TEST
/* testbygge: spara ljuden som WAV för att kunna lyssna på datorn */
void sound_dump(const char *dir);
#endif

#endif
