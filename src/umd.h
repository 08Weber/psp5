/* psp5 – made by Mille Weber */
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

#endif
