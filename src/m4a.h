/* psp5 – made by 08Weber */
#ifndef M4A_H
#define M4A_H

#include <psptypes.h>
#include <pspiofilemgr.h>

/* M4A (AAC i MP4, issue #44): läser iTunes-taggarna och omslaget, och hittar var varje
 * AAC-bildruta ligger i filen. Själva avkodningen gör music.c med sceAudiocodec.
 * ALAC och andra kodekar i M4A stöds inte. */

typedef struct {
    u32 rate;        /* samplingsfrekvens ur AudioSpecificConfig (kärnans, även för HE-AAC) */
    int channels;    /* 1 eller 2 */
    int object_type; /* 2 = AAC-LC, 5 = HE-AAC (SBR) … */
    int freq_index;  /* 0–12, 15 = frekvensen står inte i tabellen (ingen ADTS möjlig) */
    float duration;  /* sekunder, 0 = okänt */
    u32 nsamples;    /* antal bildrutor */
    u32 fixed_size;  /* alla bildrutor lika stora (stsz), annars 0 och sizes används */
    u32 *sizes;
    u32 nchunks;
    u32 *chunk_off;
    u32 nstsc;
    u32 *stsc;       /* par: första chunk (1-baserat), bildrutor per chunk */
    /* läsposition */
    u32 cur, cur_chunk, cur_in_chunk, cur_off, cur_stsc;
} M4a;

/* Titel, artist, album och spårnummer ur taggarna. Returnerar 1 om filen är AAC i MP4. */
int m4a_read_tags(SceUID fd, u32 file_size, char *title, int title_size, char *artist, int artist_size,
                  char *album, int album_size, int *trackno);
/* Omslaget (covr): JPEG- eller PNG-data som frigörs med free(), eller NULL */
void *m4a_cover(const char *path, u32 *size);

/* Läser ljudspårets tabeller. 0 = klart, <0 = inte AAC eller trasig fil. */
int m4a_open(M4a *m, SceUID fd, u32 file_size);
void m4a_free(M4a *m);
/* Ställer läspositionen på bildruta n (0 = början) */
void m4a_seek(M4a *m, u32 n);
/* Nästa bildruta: var den ligger och hur stor den är. 0 = slut. */
int m4a_next(M4a *m, u32 *offset, u32 *size);

#endif
