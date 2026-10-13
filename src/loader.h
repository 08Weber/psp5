/* psp5 – made by Mille Weber */
#ifndef LOADER_H
#define LOADER_H

#include "gfx.h"
#include "scan.h"

enum { SLOT_EMPTY, SLOT_LOADING, SLOT_READY, SLOT_MISSING };

typedef struct {
    volatile int state;
    Texture *tex;
    u32 accent;
    float shown; /* 0→1, för intoning (används av UI:t) */
} IconSlot;

/* En flik: index i spellistan i visningsordning. Måste leva tills loader_stop. */
typedef struct {
    int *idx;
    int count;
} LoaderView;

/* Startar laddartråden för listan (som måste leva tills loader_stop). */
void loader_start(const EntryList *list);
/* Stoppar tråden och frigör alla texturer som laddaren äger. */
void loader_stop(void);

/* Byter vilken flik som laddas i ordning. */
void loader_set_view(const LoaderView *view);
/* Position i fliken som är i fokus; frigör ikoner långt bort. Anropas varje bildruta. */
void loader_focus(int view_pos);
/* Ikonplatsen för ett index i spellistan. */
IconSlot *loader_icon(int list_index);

/* Be om bakgrund för posten (index i spellistan, -1 = ingen). */
void loader_request_bg(int list_index);
/* Hämtar en färdigladdad bakgrund. Returnerar 1 och tar över ägarskapet av *tex
 * (kan vara NULL om posten saknar bild). */
int loader_take_bg(int *list_index, Texture **tex);

/* Be om omslaget till låt nummer `track` i musiklistan, och hämta det när det är klart
 * (tar över ägarskapet, *tex kan vara NULL om låten saknar omslag). */
void loader_request_cover(int track);
int loader_take_cover(int *track, Texture **tex);

#endif
