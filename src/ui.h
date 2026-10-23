/* psp5 – made by Mille Weber */
#ifndef UI_H
#define UI_H

#include "gfx.h"
#include "scan.h"
#include "loader.h"

enum { UI_NONE, UI_LAUNCH, UI_EXIT, UI_SETTINGS, UI_ACTION /* Control Center: ui->cc_action (SA_*) */ };
enum { TAB_GAMES, TAB_APPS, TAB_LIBRARY, TAB_MUSIC, TAB_COUNT };

typedef struct {
    u32 pressed; /* nedtryckta denna bildruta */
    u32 held;
    u8 lx, ly;   /* analog spak, 128 = mitten */
} UiInput;

typedef struct {
    const EntryList *list;
    LoaderView views[TAB_COUNT]; /* Spel / Appar / Bibliotek (Musik har egen lista, se music.h) */
    int tab;
    int sel[TAB_COUNT];  /* position i respektive flik */
    float pos;           /* animerad scrollposition i aktiv flik */
    float tab_anim;      /* 0→1 efter flikbyte */
    float time;
    float intro;         /* 0→1 vid start */
    float launch;        /* <0 = inaktiv, annars 0→1 */
    int confirm_circle;  /* systeminställning: ○ bekräftar */
    int can_launch;      /* 0 = ARK saknas (t.ex. PPSSPP) */

    /* bakgrund: gammal + ny med övertoning */
    Texture *bg_old, *bg_cur;
    float bg_fade;
    int bg_index;
    float still;         /* sekunder sedan markeringen flyttades */

    float accent[3];     /* animerad färgton från vald ikon (0–255) */

    /* titeln bredvid valt kort, anpassad till bredden */
    int label_for;
    char label[160];
    float label_size;
    float label_anim;

    /* knapprepetition */
    int repeat_dir;
    float repeat_timer, repeat_held;

    /* biblioteket: rutnät med allt */
    int lib_sort;        /* 0 = namn, 1 = senast tillagda */
    float grid_scroll;   /* animerad översta rad */
    int grid_target;     /* raden som ska stå överst */
    char lib_label[160];
    int lib_label_for;

    /* musik */
    int music_sel;
    float music_scroll;
    int music_target;
    int seek_dir;        /* ← / → som hålls i musikfliken: -1/1, 0 = ingen */
    int seeking;         /* hållen länge nog: spolar i stället för att byta låt */
    float seek_held, seek_pos;
    float seek_show;     /* visa målet en stund efter att knappen släppts */

    /* omslag till låten som spelas */
    Texture *cover;
    int cover_for;       /* låtens index, -1 = inget */
    float cover_fade;

    /* spelets video i valt kort (preview.c): tonas in när första bilden finns */
    float preview_anim;

    /* □ i Spel/Appar: en liten alternativmeny för valt kort */
    int menu_open;
    float menu_anim;     /* 0→1 in-/uttoning */

    /* spelets logga (PIC0) – laddas och tonas in med bakgrunden */
    Texture *logo_cur;

    /* kategorier (mappar i ISO/ och PSP/GAME/): filter per flik, 0 = alla */
    int cat[TAB_COUNT];
    int cat_open;        /* SELECT: kategoriväljaren */
    float cat_anim;
    int cat_sel;         /* rad i väljaren */
    float cat_scroll, cat_still;
    Texture *cat_bg;     /* kategorimappens bg.png */
    int cat_bg_for;      /* raden bilden gäller, -1 = ingen */
    float cat_bg_fade;

    /* Control Center (HOME, som PS-knappen på PS5) */
    int cc_open;
    float cc_anim;
    int cc_sel;
    float cc_pos;        /* markeringen glider mot cc_sel */
    float cc_label;      /* 0→1: namnet på valt alternativ tonas in */
    float cc_leave;      /* >0: tonar till svart innan åtgärden körs, 1 = klart */
    int cc_confirm;      /* valt alternativ väntar på ett andra tryck */
    int cc_action;       /* SA_* som main.c ska utföra efter UI_ACTION */

    char toast[96];
    float toast_time;
} Ui;

void ui_init(Ui *ui, const EntryList *list, int confirm_circle, int can_launch);
/* Sparar flik, kategori och markering (<data>/last.txt), så att psp5 öppnar där man var. */
void ui_free(Ui *ui);
/* Spellistan har ändrats först (UMD-skivan togs ut och/eller sattes i).
 * Bygger om flikarna och behåller markeringen. Laddaren ska redan vara omstartad. */
void ui_relist(Ui *ui, int removed_front, int added_front);
int ui_update(Ui *ui, const UiInput *in, float dt);
void ui_draw(Ui *ui);
void ui_toast(Ui *ui, const char *msg);
/* Öppnar Control Center (HOME) och stänger andra paneler. */
void ui_open_cc(Ui *ui);
/* Låter korten glida in igen som vid start (efter startskärmen). */
void ui_replay_intro(Ui *ui);
/* Index i spellistan för markerad post, eller -1. */
int ui_selected(const Ui *ui);

/* Enkel skärm under första skanningen. */
void ui_draw_scanning(int done, const char *name, float time);

#endif
