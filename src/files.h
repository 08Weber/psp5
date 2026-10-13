/* psp5 – made by Mille Weber */
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

#endif
