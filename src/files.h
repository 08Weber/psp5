/* psp5 – made by 08Weber */
#ifndef FILES_H
#define FILES_H

#include "ui.h"

/* Filhanteraren (Inställningar → Ström och USB): bläddra på minneskortet (och internminnet
 * på PSP Go), kopiera, flytta, byt namn, radera och skapa mappar. Rör aldrig flash0. */
void files_open(void);
/* 1 = stäng filhanteraren */
int files_update(const UiInput *in, float dt);
void files_draw(void);
/* Avbryter en pågående åtgärd och frigör allt */
void files_close(void);

enum { FO_NONE, FO_MUSIC, FO_GAME };
/* Hämtar en fil som användaren valde att öppna i filhanteraren (t.ex. MP3 eller spel). */
int files_take_open(char *out, int size);

#endif
