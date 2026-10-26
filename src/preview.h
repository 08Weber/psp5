/* psp5 – made by 08Weber */
#ifndef PREVIEW_H
#define PREVIEW_H

#include "gfx.h"
#include "scan.h"

/* Spelets förhandsvisning som i XMB: ICON1.PMF (kort video i kortet) och SND0.AT3 (musik)
 * för valt spel. Av som standard (g_previews). Allt läses och avkodas i egna trådar. */

/* Vilket spel som ska förhandsvisas (index i spellistan), eller NULL/-1 för inget.
 * Anropas varje bildruta från huvudtråden; startar trådarna första gången. */
void preview_want(const Entry *e, int list_index);
/* 1 = inget spelas just nu (t.ex. medan inställningarna är öppna) */
void preview_pause(int paused);
/* Senaste videobilden för spelet, eller NULL. Ägs av preview.c. */
Texture *preview_frame(int list_index);
/* Stoppar allt och frigör moduler och minne – inför spelstart, USB-läge m.m. */
void preview_stop(void);
/* Felsökning: en rad till <data>/preview.log (stängs direkt, finns kvar efter en frysning).
 * shortcut: bara för testbyggen på riktig PSP, tas bort före släpp. */
void preview_trace(const char *fmt, ...);

#endif
