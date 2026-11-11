/* psp5 – made by 08Weber */
#ifndef LAUNCH_H
#define LAUNCH_H

#include "scan.h"

/* 1 om ARK:s systemctrl finns (riktig PSP med CFW), 0 t.ex. i PPSSPP. */
int launch_supported(void);

/* Startar spelet/appen. Kopierar allt den behöver från `e` först, så anroparen
 * får frigöra listan innan. Returnerar bara vid fel. */
int launch_entry(const Entry *e);

/* UMD Video-ISO (ISO/VIDEO, issue #27): ARK monterar den som UMD-skiva i XMB vid nästa
 * omstart av XMB (sctrlKernelExitVSH, som sys_exit_xmb gör). Returnerar <0 om det inte går
 * (ingen ARK, eller ISO:n är ingen videoskiva) – då är inget ändrat. */
int launch_umd_video(const char *iso_path);

#endif
