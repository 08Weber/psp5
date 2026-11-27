/* psp5 – made by 08Weber */
#ifndef THEME_H
#define THEME_H

#include "gfx.h"

/* Bakgrundens mönster */
enum { DECO_SPOT, DECO_PS1, DECO_PS2, DECO_PS3, DECO_PS4, DECO_ANNIV, DECO_ANNIV_DARK };

/* Gränssnittets färgroller (text, ytor, markering, reglage) för teman med egen palett.
 * Alla färger är ogenomskinliga RGBA(); ritvägen lägger på sin egen alfa/toning.
 *
 * De befintliga mörka temana har ink = NULL: varje ritväg använder då exakt den färg
 * den alltid har använt (se THEME_INK), så de ser pixel för pixel ut som förut. Det
 * behövs eftersom samma roll i dag har något olika värden i olika vyer (t.ex. huvudtext
 * 242/242/246 i ui.c men 238/238/242 i settings.c och files.c). */
typedef struct {
    u32 text, text_dim, text_muted; /* text på temats bakgrund och ytor */
    u32 text_inverse;               /* text på mörka ytor (grafit, OSD, dimmer) */
    u32 panel, panel_alt;           /* kort/paneler och sekundära ytor */
    u32 panel_deep, panel_hi;       /* betonad yta och ljus framhävd yta */
    u32 border, border_hi;          /* tunna kanter och ljusa skiljelinjer */
    u32 graphite, graphite_soft;    /* mörka överlägg: OSD, Control Center, meny, toast */
    u32 shadow;                     /* diskret djup */
    u32 sel_fill, sel_ink, sel_edge;/* markerat objekt: yta, text/ikon, kontrastkant */
    u32 toggle_off, knob;           /* avslaget reglage och reglagets knopp */
    u32 sym[4];                     /* handkontrollens symboler, se THEME_SYM_* */
    u32 ok, warn, err;              /* status, underordnade det grå */
    u32 fg_hi;                      /* högkontrastfärg för ikoner, pilar, mätare och knappsymboler */
    u32 pill_fill, pill_ink;        /* primärknapp/badge: fyllning och text */
    u32 toggle_on;                  /* påslaget reglage och förloppsindikator */
    u32 inv_dim;                    /* dämpad text på mörka överlägg */
    u8 dark;                        /* 1 = mörkt tema med egen ink-palett */
} ThemeInk;

/* sym[]-ordning */
enum { THEME_SYM_CROSS, THEME_SYM_SQUARE, THEME_SYM_CIRCLE, THEME_SYM_TRIANGLE };

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
    const ThemeInk *ink;/* gränssnittets färgroller, NULL = ritvägens egna (mörka) färger */
} Theme;

enum { THEME_FIRST_CLASSIC = 14 }; /* PS1–PS4 kommer efter PS5:s färger och Anniversary */

/* Färgroll för ett tema: temats egen roll om det har en palett, annars `legacy` –
 * ritvägens nuvarande färg, oförändrad för alla befintliga teman. */
#define THEME_INK(t, role, legacy) ((t)->ink ? (t)->ink->role : (legacy))

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
/* Knappsymbol i temats färger: befintliga teman använder exakt plate/mark.
 * on_dark = 1 på mörk yta/artwork (ingen extra mörk ytterkant runt ljus knapp). */
void theme_button(const Theme *t, float cx, float cy, float r, int kind, int on_dark, u32 plate, u32 mark);
/* Markerad rad/kapsel: befintliga teman ritar legacy_fill, ljusa teman får sel_edge + sel_fill. */
void theme_sel_box(const Theme *t, float x, float y, float w, float h, float r, u32 legacy_fill);

/* Frigör eventuella cachade bakgrundstexturer (t.ex. 30th Anniversary). */
void theme_term(void);

#endif
