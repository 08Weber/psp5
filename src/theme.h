/* psp5 – made by Mille Weber */
#ifndef THEME_H
#define THEME_H

#include "gfx.h"

/* Bakgrundens mönster */
enum { DECO_SPOT, DECO_PS1, DECO_PS2, DECO_PS3, DECO_PS4 };

/* Ett färgtema: PS5:s egna konsolfärger (skalen) och de fyra äldre konsolernas
 * utseende, som PS5:s inställning Utseende. Färgerna är RGBA() (ABGR). */
typedef struct {
    const char *code;   /* i psp5.cfg (theme=...) */
    const char *name;   /* produktnamn, översätts inte */
    u32 top, bottom;    /* bakgrund när spelet saknar bild */
    u32 stop, sbottom;  /* inställningarnas bakgrund (lite ljusare) */
    u32 glow;           /* mjukt ljus uppe till vänster */
    u32 haze;           /* svagt dis längs nederkanten */
    u32 sheen;          /* andra ljuset nere till höger (Chroma), 0 = inget */
    u32 accent;         /* reglage, förlopp, laddningsprickar */
    u32 card;           /* kort som inte har laddat sin bild än */
    u8 deco;
    u8 tone;            /* hur mycket valt spels färgton färgar bakgrunden (0–100 %) */
} Theme;

enum { THEME_FIRST_CLASSIC = 12 }; /* PS1–PS4 kommer efter PS5:s färger */

/* Valt tema (index) och om spelens bilder visas bakom korten. Sparas i psp5.cfg. */
extern unsigned char g_theme;
extern unsigned char g_theme_art;

int theme_count(void);
const Theme *theme_get(int i);
const Theme *theme_cur(void);
/* "cobalt" → index, okänd kod → 0 (PS5). Läser till radslut. */
int theme_from_code(const char *code);

/* Hela bakgrunden: toning, ljus och temats mönster. `tone` = valt spels färg (0–255),
 * `settings` = inställningarnas ljusare variant, `a` = synlighet för mönstret. */
void theme_backdrop(const Theme *t, float time, const float tone[3], int settings, float a);
/* Liten förhandsvisning (för inställningarna) */
void theme_swatch(const Theme *t, float x, float y, float w, float h, float a);
/* Temats färg för ett påslaget reglage */
u32 theme_toggle_on(const Theme *t);

#endif
