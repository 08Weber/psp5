/* psp5 – made by 08Weber */
#ifndef SOUND_H
#define SOUND_H

/* PlayStation 5-ljud (förpackade i ps5_sounds.h via tools/make_sounds.ps1). */
typedef enum {
    SND_MOVE,       /* bläddra mellan kort och rader */
    SND_SWIPE,      /* byta huvudflik (L / R) */
    SND_PANEL,      /* byta panel / vy / sortering */
    SND_SELECT,     /* välja / gå in */
    SND_BACK,       /* tillbaka / avbryt */
    SND_TOGGLE_ON,  /* slå på reglage / markera */
    SND_TOGGLE_OFF, /* slå av reglage / avmarkera */
    SND_SLIDER,     /* ändra värde / flyttsteg */
    SND_OPEN,       /* öppna alternativ, kategorier eller inställningar */
    SND_CLOSE_MENU, /* stänga alternativ, kategorier eller inställningar */
    SND_CC_OPEN,    /* öppna Control Center */
    SND_CC_CLOSE,   /* stänga Control Center */
    SND_LAUNCH,     /* starta spel eller app */
    SND_ERROR,      /* något gick inte */
    SND_SCREENSHOT, /* skärmbild tagen */
    SND_KEY_MOVE,   /* flytta markören på skärmtangentbordet */
    SND_KEY_TYPE,   /* skriva tecken i sökningen */
    SND_KEY_BACK,   /* ta bort tecken i sökningen */
    SND_KEY_ERROR,  /* fel i textruta (tom / full) */
    SND_CONFIRM,    /* bekräfta åtgärd / tema / dialog */
    SND_BOOT,       /* startskärmen */
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
/* Räknar fram startljudet igen i bakgrunden (startskärmen efter viloläge).
 * sound_ready() är 0 tills det är klart. */
void sound_prepare_boot(void);
/* Startljudet räknas inte fram vid start (felsäker start utan startskärm).
 * Anropas före sound_init. */
void sound_skip_boot(void);
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
