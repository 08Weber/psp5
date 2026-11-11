/* psp5 – made by 08Weber */
#ifndef UMD_H
#define UMD_H

#include "scan.h"

#ifdef PSP5_TEST
/* testbygge: PPSSPP har ingen skiva när den kör homebrew – mappen låtsas vara disc0: */
#define UMD_ROOT "ms0:/PSP5/fakeumd/"
#else
#define UMD_ROOT "disc0:/"
#endif

/* Håller koll på UMD-enheten i en egen tråd. När en spelskiva sätts i läses
 * titel, spel-ID och bilder in, så att skivan kan visas först bland spelen. */
void umd_start(void);
void umd_stop(void);

/* Har skivan satts i eller tagits ut sedan förra anropet? Returnerar 1 i så fall:
 * *has = 1 och *e = skivans post, eller *has = 0 om ingen spelskiva sitter i. */
int umd_poll(Entry *e, int *has);

/* UMD Video-skiva i enheten (issue #27)? 1 och titeln (UMD_VIDEO/PARAM.SFO) i så fall.
 * Spelas i XMB – psp5 visar den bara i Videor. */
int umd_video(char *title, int size);
/* Ökar när en videoskiva sätts i eller tas ut – Videor läses då om */
int umd_video_generation(void);

#endif
