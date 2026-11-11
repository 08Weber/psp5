/* psp5 – made by 08Weber */
#ifndef MUSIC_H
#define MUSIC_H

#include <psptypes.h>

/* Musikspelare som XMB:s musik: MP3 och M4A (AAC) från ms0:/MUSIC (även undermappar),
 * avkodas med PSP:ns egna avkodare (sceMp3, sceAudiocodec) i en egen tråd, så att musiken
 * fortsätter medan man bläddrar i psp5. Nästa låt startar av sig själv. */

typedef struct {
    char path[256];
    char title[96];  /* ID3-/M4A-titel, annars filnamnet utan .mp3/.m4a */
    char artist[64]; /* ID3-/M4A-artist, annars "" */
    char album[64];  /* ID3-/M4A-album, annars mappnamnet ("" i ms0:/MUSIC) */
} Track;

enum { MUSIC_STOPPED, MUSIC_PLAYING, MUSIC_PAUSED };

/* Laddar avkodarmodulerna och startar spelartråden, som först läser in låtlistan
 * i bakgrunden (sorterad efter artist, album, spår, titel). */
void music_init(void);
/* Stoppar uppspelningen, tråden och frigör allt (även låtlistan). */
void music_term(void);

int music_scanning(void);     /* 1 medan låtlistan läses in */
int music_generation(void);   /* ändras när listan byts – för UI:ts cachar */
int music_needs_cjk(void);    /* 1 om någon titel/artist behöver det japanska typsnittet */
int music_count(void);
/* Omslaget ur låtens ID3-tagg (APIC/PIC, helst framsidan) eller M4A-tagg (covr): JPEG- eller PNG-data som
 * ska frigöras med free(), eller NULL. Läser filen – anropas inte från spelartråden. */
void *music_cover_data(const char *path, u32 *size);
const Track *music_track(int index);

void music_play(int index);   /* börjar om från början */
void music_play_path(const char *path); /* spelar en viss MP3-/M4A-fil (t.ex. öppnad från filhanteraren) */
void music_toggle_pause(void);
void music_stop(void);
void music_next(void);
void music_prev(void);        /* från början om > 3 s spelats, annars föregående */
void music_seek(float sec);   /* hoppar i aktuell låt (pausad förblir pausad) */

int music_state(void);        /* MUSIC_STOPPED / _PLAYING / _PAUSED */
int music_current(void);      /* index för aktuell låt, -1 = ingen */
float music_elapsed(void);    /* sekunder */
float music_duration(void);   /* sekunder (uppskattat), 0 = okänt */

#endif
