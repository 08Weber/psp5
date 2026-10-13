/* psp5 – made by Mille Weber */
#ifndef UI_H
#define UI_H

#include "gfx.h"
#include "scan.h"
#include "loader.h"

enum { UI_NONE, UI_LAUNCH, UI_EXIT, UI_SETTINGS };
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

    /* □ i Spel/Appar: en liten alternativmeny för valt kort */
    int menu_open;
    float menu_anim;     /* 0→1 in-/uttoning */

    char toast[96];
    float toast_time;
} Ui;

void ui_init(Ui *ui, const EntryList *list, int confirm_circle, int can_launch);
void ui_free(Ui *ui);
/* Spellistan har ändrats först (UMD-skivan togs ut och/eller sattes i).
 * Bygger om flikarna och behåller markeringen. Laddaren ska redan vara omstartad. */
void ui_relist(Ui *ui, int removed_front, int added_front);
int ui_update(Ui *ui, const UiInput *in, float dt);
void ui_draw(Ui *ui);
void ui_toast(Ui *ui, const char *msg);
/* Låter korten glida in igen som vid start (efter startskärmen). */
void ui_replay_intro(Ui *ui);
/* Index i spellistan för markerad post, eller -1. */
int ui_selected(const Ui *ui);

/* Enkel skärm under första skanningen. */
void ui_draw_scanning(int done, const char *name, float time);

#endif
