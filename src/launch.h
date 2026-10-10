#ifndef LAUNCH_H
#define LAUNCH_H

#include "scan.h"

/* 1 om ARK:s systemctrl finns (riktig PSP med CFW), 0 t.ex. i PPSSPP. */
int launch_supported(void);

/* Startar spelet/appen. Kopierar allt den behöver från `e` först, så anroparen
 * får frigöra listan innan. Returnerar bara vid fel. */
int launch_entry(const Entry *e);

#endif
