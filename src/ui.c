/* psp5 – made by Mille Weber */
/* psp5:s utseende: valt spels bild över hela skärmen, en rad kort högst upp
 * där valt kort är stort med tunn ljus ram, flikar för spel/appar/bibliotek
 * (biblioteket visar allt i ett rutnät),
 * neutral ljus typografi och mjuka animationer. All grafik ritas här. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <psppower.h>
#include <psprtc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ui.h"
#include "config.h"
#include "sound.h"
#include "lang.h"
#include "music.h"
#include "system.h"
#include "theme.h"
#include "osd.h"
#include "testrun.h"

/* layout (480×272) */
#define MARGIN     24.0f
#define ROW_Y      38.0f   /* kortradens överkant */
#define BIG_W      112.0f
#define BIG_H      62.0f
#define SMALL_W    60.0f
#define SMALL_H    33.0f
#define GAP        6.0f
#define SMALL_X    (MARGIN + BIG_W + 10)  /* första lilla kortet */
#define STEP       (SMALL_W + GAP)
#define BIG_R      9.0f
#define LABEL_Y    (ROW_Y + SMALL_H + 26) /* titeln under de små korten */

/* biblioteket: rutnät med 5 kolumner */
#define GRID_COLS  5
#define GRID_GAP   10.0f
#define GRID_Y     64.0f   /* första radens överkant */
#define CELL_W     ((SCREEN_W - 2 * MARGIN - (GRID_COLS - 1) * GRID_GAP) / GRID_COLS)
#define CELL_H     (CELL_W * 80.0f / 144.0f) /* ikonernas format */
#define ROW_STEP   (CELL_H + 11.0f)
#define GRID_ROWS  3       /* hela rader som syns */
#define GRID_BOTTOM 234.0f /* rutnätet klipps här, ovanför titel och knapptips */

/* musik: låtlista till vänster, "spelas nu" till höger */
#define MLIST_Y    50.0f
#define MROW_H     26.0f
#define MROWS      7
#define PANEL_X    306.0f

#define BG_DELAY   0.18f /* stå still så här länge innan bakgrunden laddas */
#define BG_FADE    3.5f  /* övertoningshastighet (1/s) */
#define LAUNCH_LEN 0.45f
#define SEEK_HOLD  0.35f /* så länge ← / → hålls innan musiken spolar */

#define CLEAR_COLOR RGBA(10, 10, 14, 255)
#define TEXT_MAIN   RGBA(242, 242, 246, 255)
#define TEXT_DIM    RGBA(165, 168, 178, 255)
#define LIGHT       RGBA(238, 238, 242, 255)
#define DARK        RGBA(18, 18, 22, 255)

static const StrId TAB_NAMES[TAB_COUNT] = { S_TAB_GAMES, S_TAB_APPS, S_TAB_LIBRARY, S_TAB_MUSIC };

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
/* bildfrekvensoberoende utjämning mot målet */
static float approach(float cur, float target, float speed, float dt)
{
    return lerpf(cur, target, 1.0f - expf(-speed * dt));
}
static u32 with_alpha(u32 c, float a)
{
    return COLOR_ALPHA(c, (int)(((c >> 24) & 0xFF) * clampf(a, 0, 1)));
}

/* --- flikar --- */

/* Kort som användaren flyttat med □ till den andra fliken (homebrew till Spel, eller tvärtom),
 * sparade i <data>/moved.txt: en rad per kort (entry_cover_key). s_moved börjar med '\n'. */
static char s_moved[4096] = "\n";

static void moved_path(char *out, int size)
{
    snprintf(out, size, "%s/moved.txt", g_data_dir);
}

static void moved_load(void)
{
    char path[64];
    moved_path(path, sizeof(path));
    s_moved[1] = 0;
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return;
    int n = sceIoRead(fd, s_moved + 1, sizeof(s_moved) - 2);
    sceIoClose(fd);
    s_moved[n > 0 ? n + 1 : 1] = 0;
}

static char *moved_find(const Entry *e)
{
    char key[80];
    entry_cover_key(e, key + 1, sizeof(key) - 2);
    key[0] = '\n';
    strcat(key, "\n");
    return strstr(s_moved, key);
}

/* Flyttar kortet till den andra fliken och sparar. 0 = listan är full. */
static int moved_toggle(const Entry *e)
{
    char *p = moved_find(e);
    if (p) {
        char *next = strchr(p + 1, '\n');
        memmove(p, next, strlen(next) + 1);
    } else {
        char key[80];
        entry_cover_key(e, key, sizeof(key));
        int len = strlen(s_moved);
        if (len + (int)strlen(key) + 2 > (int)sizeof(s_moved))
            return 0;
        snprintf(s_moved + len, sizeof(s_moved) - len, "%s\n", key);
    }
    char path[64];
    moved_path(path, sizeof(path));
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, s_moved + 1, strlen(s_moved + 1));
        sceIoClose(fd);
    }
    return 1;
}

static int is_app(const Entry *e)
{
    return (e->type == ENTRY_HOMEBREW) != (moved_find(e) != NULL);
}

static const LoaderView *view(const Ui *ui)
{
    return &ui->views[ui->tab];
}

int ui_selected(const Ui *ui)
{
    const LoaderView *v = view(ui);
    return v->count ? v->idx[ui->sel[ui->tab]] : -1;
}

/* biblioteket sorteras efter namn eller efter när filen lades till – UMD-skivan alltid först */
static const EntryList *s_sort_list;
static int cmp_name(const void *a, const void *b)
{
    const Entry *x = &s_sort_list->items[*(const int *)a], *y = &s_sort_list->items[*(const int *)b];
    if ((x->type == ENTRY_UMD) != (y->type == ENTRY_UMD))
        return x->type == ENTRY_UMD ? -1 : 1;
    int c = strcasecmp(x->title, y->title);
    return c ? c : *(const int *)a - *(const int *)b;
}
static int cmp_recent(const void *a, const void *b)
{
    const Entry *x = &s_sort_list->items[*(const int *)a], *y = &s_sort_list->items[*(const int *)b];
    if ((x->type == ENTRY_UMD) != (y->type == ENTRY_UMD))
        return x->type == ENTRY_UMD ? -1 : 1;
    if (x->file_time != y->file_time)
        return x->file_time > y->file_time ? -1 : 1;
    return cmp_name(a, b);
}

/* Spel och Appar "som XMB": nyaste mappen först – ordningen Homebrew Sorter sätter (issue #7) */
static int cmp_xmb(const void *a, const void *b)
{
    const Entry *x = &s_sort_list->items[*(const int *)a], *y = &s_sort_list->items[*(const int *)b];
    if ((x->type == ENTRY_UMD) != (y->type == ENTRY_UMD))
        return x->type == ENTRY_UMD ? -1 : 1;
    if (x->sort_time != y->sort_time)
        return x->sort_time > y->sort_time ? -1 : 1;
    return *(const int *)a - *(const int *)b; /* listan är redan A–Ö */
}

static void sort_library(Ui *ui)
{
    LoaderView *v = &ui->views[TAB_LIBRARY];
    if (!v->idx || v->count < 2)
        return;
    s_sort_list = ui->list;
    qsort(v->idx, v->count, sizeof(int), ui->lib_sort ? cmp_recent : cmp_name);
}

static void build_views(Ui *ui)
{
    for (int t = 0; t < TAB_COUNT; t++) {
        ui->views[t].idx = malloc((ui->list->count ? ui->list->count : 1) * sizeof(int));
        ui->views[t].count = 0;
    }
    for (int i = 0; i < ui->list->count; i++) {
        LoaderView *v = &ui->views[is_app(&ui->list->items[i]) ? TAB_APPS : TAB_GAMES];
        if (v->idx)
            v->idx[v->count++] = i;
        LoaderView *lib = &ui->views[TAB_LIBRARY];
        if (lib->idx)
            lib->idx[lib->count++] = i;
    }
    s_sort_list = ui->list;
    for (int t = TAB_GAMES; g_sort_xmb && t <= TAB_APPS; t++)
        if (ui->views[t].idx)
            qsort(ui->views[t].idx, ui->views[t].count, sizeof(int), cmp_xmb);
    sort_library(ui);
}

/* □ i biblioteket: byt sortering men behåll markeringen på samma spel */
static void toggle_sort(Ui *ui)
{
    LoaderView *v = &ui->views[TAB_LIBRARY];
    int chosen = ui_selected(ui);
    ui->lib_sort ^= 1;
    sort_library(ui);
    for (int p = 0; p < v->count; p++)
        if (v->idx[p] == chosen)
            ui->sel[TAB_LIBRARY] = p;
    ui->tab_anim = 0.4f;
    ui->lib_label_for = -2;
    sound_play(SND_TOGGLE);
}

static void switch_tab(Ui *ui, int tab)
{
    if (tab == ui->tab || tab < 0 || tab >= TAB_COUNT)
        return;
    ui->tab = tab;
    ui->tab_anim = 0;
    sound_play(SND_SWIPE);
    ui->pos = ui->sel[tab] + 0.7f; /* raden glider in från höger */
    ui->still = 0;
    ui->label_for = -2;
    ui->seek_dir = ui->seeking = 0; /* en hållen pil gäller bara i musikfliken */
    if (tab != TAB_MUSIC) /* musikfliken har inga kort – låt ikonerna ligga kvar */
        loader_set_view(view(ui));
}

/* --- färgton --- */

static void accent_target(u32 avg, float out[3])
{
    float c[3] = { avg & 0xFF, (avg >> 8) & 0xFF, (avg >> 16) & 0xFF };
    float mx = fmaxf(c[0], fmaxf(c[1], c[2])), mn = fminf(c[0], fminf(c[1], c[2]));
    if (mx - mn < 18) {
        out[0] = out[1] = out[2] = 120;
        return;
    }
    for (int k = 0; k < 3; k++)
        out[k] = 60 + (c[k] - mn) / (mx - mn) * 160;
}

/* --- titel --- */

static void utf8_pop(char *s)
{
    int n = strlen(s);
    while (n > 0) {
        n--;
        if (((u8)s[n] & 0xC0) != 0x80)
            break;
    }
    s[n] = 0;
}

static void fit_label(Ui *ui)
{
    int i = ui_selected(ui);
    const float max_w = SCREEN_W - SMALL_X - MARGIN;
    ui->label_for = i;
    ui->label[0] = 0;
    if (i < 0)
        return;
    const char *title = ui->list->items[i].title;
    ui->label_size = 0.78f;
    snprintf(ui->label, sizeof(ui->label), "%s", title);
    while (ui->label_size > 0.62f && text_width(ui->label_size, 0, ui->label) > max_w)
        ui->label_size -= 0.04f;
    if (text_width(ui->label_size, 0, ui->label) > max_w) {
        char buf[sizeof(ui->label)];
        snprintf(buf, sizeof(buf), "%s", title);
        do {
            utf8_pop(buf);
            snprintf(ui->label, sizeof(ui->label), "%s...", buf);
        } while (buf[0] && text_width(ui->label_size, 0, ui->label) > max_w);
    }
}

/* --- livscykel --- */

void ui_init(Ui *ui, const EntryList *list, int confirm_circle, int can_launch)
{
    memset(ui, 0, sizeof(*ui));
    ui->list = list;
    ui->confirm_circle = confirm_circle;
    ui->can_launch = can_launch;
    ui->launch = -1;
    ui->bg_index = -1;
    ui->label_for = -2;
    ui->lib_label_for = -2;
    ui->cover_for = -2;
    ui->tab_anim = 1;
    ui->accent[0] = ui->accent[1] = ui->accent[2] = 120;
    moved_load();
    build_views(ui);
    ui->tab = ui->views[TAB_GAMES].count || !ui->views[TAB_APPS].count ? TAB_GAMES : TAB_APPS;
    ui->pos = 1.5f; /* korten glider in vid start */
    loader_set_view(view(ui));
}

/* Spellistans index före → efter att första posten tagits bort och/eller lagts till. */
static int remap_index(int i, int removed_front, int added_front)
{
    if (i < 0 || (removed_front && i == 0))
        return -1;
    return i - removed_front + added_front;
}

void ui_relist(Ui *ui, int removed_front, int added_front)
{
    int chosen[TAB_COUNT];
    int old_sel = ui->sel[ui->tab];
    for (int t = 0; t < TAB_COUNT; t++) {
        LoaderView *v = &ui->views[t];
        chosen[t] = -1;
        if (t != TAB_MUSIC && v->count && v->idx)
            chosen[t] = remap_index(v->idx[ui->sel[t]], removed_front, added_front);
        /* står man först i fliken blir en nyinsatt skiva markerad */
        if (added_front && (t == TAB_GAMES || t == TAB_LIBRARY) && ui->sel[t] == 0)
            chosen[t] = 0;
        free(v->idx);
        v->idx = NULL;
        v->count = 0;
    }
    build_views(ui);
    for (int t = 0; t < TAB_COUNT; t++) {
        if (t == TAB_MUSIC)
            continue;
        LoaderView *v = &ui->views[t];
        ui->sel[t] = 0;
        for (int p = 0; p < v->count; p++)
            if (v->idx[p] == chosen[t])
                ui->sel[t] = p;
    }
    if (ui->tab != TAB_MUSIC)
        ui->pos += ui->sel[ui->tab] - old_sel; /* raden står still om samma kort är markerat */
    ui->still = 0;
    ui->bg_index = remap_index(ui->bg_index, removed_front, added_front);
    ui->label_for = -2;
    ui->lib_label_for = -2;
    ui->menu_open = 0; /* kortet menyn gällde kan ha flyttats */
    loader_set_view(view(ui));
}

/* □ i Spel/Appar: valt kort flyttas till den andra fliken, markeringen står kvar på samma plats */
static void move_selected(Ui *ui)
{
    const Entry *e = &ui->list->items[ui_selected(ui)];
    if (e->type == ENTRY_UMD || !moved_toggle(e)) {
        sound_play(SND_ERROR);
        return;
    }
    int tab = ui->tab, old = ui->sel[tab];
    ui_relist(ui, 0, 0);
    int n = ui->views[tab].count;
    ui->sel[tab] = old < n ? old : (n ? n - 1 : 0);
    ui->pos = ui->sel[tab] + 0.3f;
    ui_toast(ui, L(tab == TAB_GAMES ? S_MOVED_TO_APPS : S_MOVED_TO_GAMES));
    sound_play(SND_TOGGLE);
}

void ui_free(Ui *ui)
{
    tex_free(ui->bg_old);
    tex_free(ui->bg_cur);
    ui->bg_old = ui->bg_cur = NULL;
    tex_free(ui->cover);
    ui->cover = NULL;
    for (int t = 0; t < TAB_COUNT; t++) {
        free(ui->views[t].idx);
        ui->views[t].idx = NULL;
        ui->views[t].count = 0;
    }
}

void ui_replay_intro(Ui *ui)
{
    ui->intro = 0;
    if (ui->tab != TAB_MUSIC)
        ui->pos = ui->sel[ui->tab] + 1.5f;
}

void ui_toast(Ui *ui, const char *msg)
{
    snprintf(ui->toast, sizeof(ui->toast), "%s", msg);
    ui->toast_time = 2.8f;
}

/* --- uppdatering --- */

/* Riktning med knapprepetition: ±1 = vänster/höger, ±2 = upp/ner (bara i rutnätet) */
static int step_input(Ui *ui, const UiInput *in, float dt, int vertical)
{
    int dir = 0;
    if ((in->held & PSP_CTRL_LEFT) || in->lx < 40) dir = -1;
    if ((in->held & PSP_CTRL_RIGHT) || in->lx > 215) dir = 1;
    if (vertical && ((in->held & PSP_CTRL_UP) || in->ly < 40)) dir = -2;
    if (vertical && ((in->held & PSP_CTRL_DOWN) || in->ly > 215)) dir = 2;
    if (!dir) {
        ui->repeat_dir = 0;
        return 0;
    }
    if (dir != ui->repeat_dir) { /* ny riktning: flytta direkt */
        ui->repeat_dir = dir;
        ui->repeat_timer = 0.32f;
        ui->repeat_held = 0;
        return dir;
    }
    ui->repeat_held += dt;
    ui->repeat_timer -= dt;
    if (ui->repeat_timer <= 0) {
        ui->repeat_timer += ui->repeat_held > 1.2f ? 0.045f : 0.085f;
        return dir;
    }
    return 0;
}

static void update_background(Ui *ui, float dt)
{
    int sel = g_theme_art ? ui_selected(ui) : -1; /* bilderna avstängda: bara temats yta */
    int idx;
    Texture *tex;
    if (loader_take_bg(&idx, &tex)) {
        if (idx == sel && idx != ui->bg_index) {
            tex_free(ui->bg_old);
            ui->bg_old = ui->bg_cur;
            ui->bg_cur = tex;
            ui->bg_index = idx;
            ui->bg_fade = 0;
        } else {
            tex_free(tex); /* hann flytta vidare */
        }
    }
    if (ui->still >= BG_DELAY && sel >= 0 && ui->bg_index != sel)
        loader_request_bg(sel);
    if (sel < 0 && ui->bg_cur) { /* tom flik: tona ut bilden */
        tex_free(ui->bg_old);
        ui->bg_old = ui->bg_cur;
        ui->bg_cur = NULL;
        ui->bg_index = -1;
        ui->bg_fade = 0;
    }
    ui->bg_fade = clampf(ui->bg_fade + dt * BG_FADE, 0, 1);
    if (ui->bg_fade >= 1 && ui->bg_old) {
        tex_free(ui->bg_old);
        ui->bg_old = NULL;
    }
}

/* Omslaget till låten som spelas: begärs när låten byts och tonas in när det är laddat */
static void update_cover(Ui *ui, float dt)
{
    int cur = music_current();
    if (cur != ui->cover_for) {
        tex_free(ui->cover);
        ui->cover = NULL;
        ui->cover_for = cur;
        ui->cover_fade = 0;
        if (cur >= 0)
            loader_request_cover(cur);
    }
    int idx;
    Texture *tex;
    if (loader_take_cover(&idx, &tex)) {
        if (idx == ui->cover_for && !ui->cover)
            ui->cover = tex;
        else
            tex_free(tex); /* låten hann bytas */
    }
    if (ui->cover)
        ui->cover_fade = clampf(ui->cover_fade + dt * 4, 0, 1);
}

/* Musikfliken: ↑↓ väljer, ✕ spelar (eller pausar vald låt), □ paus, ← → föregående/nästa låt */
static void update_music(Ui *ui, const UiInput *in, float dt)
{
    int n = music_count();
    UiInput vert = *in; /* ← → ska inte upprepas när de hålls in */
    vert.held &= ~(PSP_CTRL_LEFT | PSP_CTRL_RIGHT);
    vert.lx = 128;
    int move = step_input(ui, &vert, dt, 1);
    if (move && n) {
        int next = clampf(ui->music_sel + move / 2, 0, n - 1);
        if (next != ui->music_sel) {
            ui->music_sel = next;
            sound_play(SND_MOVE);
        }
    }
    if (ui->intro < 1 || !n)
        return;
    int playing = music_current() >= 0 && music_state() != MUSIC_STOPPED;
    /* ← →: kort tryck byter låt (när knappen släpps), håll för att spola – fortare ju längre */
    int dir = (in->held & PSP_CTRL_RIGHT) ? 1 : (in->held & PSP_CTRL_LEFT) ? -1 : 0;
    if (ui->seek_show > 0)
        ui->seek_show -= dt;
    if (dir && playing) {
        if (dir != ui->seek_dir) {
            ui->seek_dir = dir;
            ui->seek_held = 0;
            ui->seeking = 0;
        }
        ui->seek_held += dt;
        float dur = music_duration();
        if (!ui->seeking && ui->seek_held >= SEEK_HOLD && dur > 0) {
            ui->seeking = 1;
            ui->seek_pos = music_elapsed();
        }
        if (ui->seeking) {
            float speed = ui->seek_held < 1.5f ? 8 : ui->seek_held < 3.0f ? 20 : 45; /* sekunder per sekund */
            /* stannar en sekund före slutet: att släppa där ska inte byta låt */
            ui->seek_pos = clampf(ui->seek_pos + dir * speed * dt, 0, dur > 1 ? dur - 1 : 0);
        }
    } else if (ui->seek_dir) { /* släppt */
        if (ui->seeking && playing) {
            music_seek(ui->seek_pos);
            ui->seek_show = 0.5f; /* tills spelartråden har hoppat */
        } else if (playing) {
            if (ui->seek_dir > 0)
                music_next();
            else
                music_prev();
        }
        ui->seek_dir = 0;
        ui->seeking = 0;
    }
    if ((in->pressed & PSP_CTRL_SQUARE) && playing)
        music_toggle_pause();
    u32 confirm = (ui->confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS) | PSP_CTRL_START;
    if (in->pressed & confirm) {
        if (playing && ui->music_sel == music_current())
            music_toggle_pause();
        else
            music_play(ui->music_sel);
    }
    if (ui->music_sel < ui->music_target)
        ui->music_target = ui->music_sel;
    if (ui->music_sel > ui->music_target + MROWS - 1)
        ui->music_target = ui->music_sel - (MROWS - 1);
    ui->music_scroll = approach(ui->music_scroll, ui->music_target, 14.0f, dt);
}

int ui_update(Ui *ui, const UiInput *in, float dt)
{
    ui->time += dt;
    ui->intro = clampf(ui->intro + dt * 2.2f, 0, 1);
    ui->tab_anim = clampf(ui->tab_anim + dt * 4.0f, 0, 1);
    if (ui->toast_time > 0)
        ui->toast_time -= dt;

    if (ui->launch >= 0) {
        ui->launch += dt / LAUNCH_LEN;
        return ui->launch >= 1 ? UI_LAUNCH : UI_NONE;
    }
    /* alternativmenyn: ✕ väljer, ○ eller □ stänger – resten av gränssnittet får inga knappar */
    ui->menu_anim = clampf(ui->menu_anim + (ui->menu_open ? 7 : -7) * dt, 0, 1);
    static const UiInput none = { 0, 0, 128, 128 };
    if (ui->menu_open) {
        u32 ok = ui->confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
        u32 back = (ui->confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE) | PSP_CTRL_SQUARE | PSP_CTRL_TRIANGLE;
        if (in->pressed & ok) {
            ui->menu_open = 0;
            move_selected(ui);
        } else if (in->pressed & back) {
            ui->menu_open = 0;
            sound_play(SND_BACK);
        }
        in = &none;
    }
    if ((in->pressed & PSP_CTRL_TRIANGLE) && ui->intro >= 1)
        return UI_SETTINGS; /* ljudet spelas när inställningarna har lästs in (main.c) */
    if (in->pressed & PSP_CTRL_LTRIGGER)
        switch_tab(ui, ui->tab - 1);
    if (in->pressed & PSP_CTRL_RTRIGGER)
        switch_tab(ui, ui->tab + 1);

    if (ui->tab == TAB_MUSIC)
        update_music(ui, in, dt);

    int count = view(ui)->count;
    int *sel = &ui->sel[ui->tab];
    int grid = ui->tab == TAB_LIBRARY;
    if (grid && (in->pressed & PSP_CTRL_SQUARE) && count > 1 && ui->launch < 0)
        toggle_sort(ui);
    if ((ui->tab == TAB_GAMES || ui->tab == TAB_APPS) && (in->pressed & PSP_CTRL_SQUARE) && count &&
        ui->intro >= 1 && ui->list->items[ui_selected(ui)].type != ENTRY_UMD) {
        ui->menu_open = 1;
        sound_play(SND_OPEN);
    }
    int move = ui->tab == TAB_MUSIC ? 0 : step_input(ui, in, dt, grid);
    if (move && count) {
        int next = *sel + move;
        if (move == 2 || move == -2) {
            next = *sel + (move > 0 ? GRID_COLS : -GRID_COLS);
            /* ner från näst sista raden till en kortare sista rad: sista kortet */
            if (next >= count && *sel / GRID_COLS < (count - 1) / GRID_COLS)
                next = count - 1;
            if (next < 0 || next >= count)
                next = *sel;
        }
        next = clampf(next, 0, count - 1);
        if (next != *sel) {
            *sel = next;
            ui->still = 0;
            sound_play(SND_MOVE);
        }
    }
    ui->still += dt;

    /* START startar också – ut till XMB går man med HOME (systemets egen dialog) */
    u32 confirm = (ui->confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS) | PSP_CTRL_START;
    if ((in->pressed & confirm) && count && ui->intro >= 1) {
        if (ui->can_launch) {
            ui->launch = 0;
            sound_play(SND_LAUNCH);
        } else {
            ui_toast(ui, L(S_NO_ARK));
            sound_play(SND_ERROR);
        }
    }

    /* rutnätet rullar så att markerad rad alltid syns helt */
    int row = count ? *sel / GRID_COLS : 0;
    if (grid) {
        if (row < ui->grid_target)
            ui->grid_target = row;
        if (row > ui->grid_target + GRID_ROWS - 1)
            ui->grid_target = row - (GRID_ROWS - 1);
        ui->grid_scroll = approach(ui->grid_scroll, ui->grid_target, 14.0f, dt);
    }

    ui->pos = approach(ui->pos, *sel, 13.0f, dt);
    if (fabsf(ui->pos - *sel) < 0.001f)
        ui->pos = *sel;
    if (ui->tab != TAB_MUSIC)
        loader_focus(*sel);
    if (music_needs_cjk())
        font_ensure_cjk(); /* japanska/ryska låttitlar (laddas bara en gång) */
    update_background(ui, dt);
    update_cover(ui, dt);

    IconSlot *slot = loader_icon(ui_selected(ui));
    float target[3] = { 120, 120, 120 };
    if (slot && slot->state == SLOT_READY)
        accent_target(slot->accent, target);
    for (int k = 0; k < 3; k++)
        ui->accent[k] = approach(ui->accent[k], target[k], 5.0f, dt);

    if (ui->label_for != ui_selected(ui)) {
        fit_label(ui);
        ui->label_anim = 0;
    }
    ui->label_anim = clampf(ui->label_anim + dt * 5.0f, 0, 1);
    return UI_NONE;
}

/* --- ritning --- */

/* Temats yta med ett mjukt ljus uppifrån – syns när spelet saknar bild (eller bilderna är avstängda). */
static void draw_spotlight(const float tone[3], float time)
{
    theme_backdrop(theme_cur(), time, tone, 0, 1);
}

static void draw_background(Ui *ui)
{
    /* en bild som täcker hela skärmen (färdigtonad, eller den gamla helt under den nya) – då behövs ingen yta under */
    if (!ui->bg_cur || (ui->bg_fade < 1 && !ui->bg_old))
        draw_spotlight(ui->accent, ui->time);

    const u32 tint = RGBA(205, 205, 210, 0);
    int fade = (int)(ui->bg_fade * 255);
    if (ui->bg_old)
        gfx_tex(ui->bg_old, 0, 0, SCREEN_W, SCREEN_H, tint | (u32)(ui->bg_cur ? 255 : 255 - fade) << 24);
    if (ui->bg_cur)
        gfx_tex(ui->bg_cur, 0, 0, SCREEN_W, SCREEN_H, tint | (u32)fade << 24);

    /* toningar: uppe (flikar/kort), vänster (text) och nere (knappar) */
    gfx_gradient_v(0, 0, SCREEN_W, 120, RGBA(0, 0, 0, 165), RGBA(0, 0, 0, 0));
    gfx_gradient_h(0, 0, 300, SCREEN_H, RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 0));
    gfx_gradient_v(0, 160, SCREEN_W, SCREEN_H - 160, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 175));
}

/* Laddas batteriet sveper fyllningen från nivån upp till fullt om och om igen,
 * och en liten blixt syns till vänster om batteriet. */
static void draw_battery(float x, float y, int percent, int charging, float time, float a)
{
    gfx_round_rect(x, y, 20, 10, 2.5f, with_alpha(LIGHT, 0.85f * a));
    gfx_round_rect(x + 1.3f, y + 1.3f, 17.4f, 7.4f, 1.8f, with_alpha(DARK, a));
    gfx_rect(x + 20, y + 3, 1.8f, 4, with_alpha(LIGHT, 0.85f * a));
    if (percent >= 0) {
        const float full = 14.8f;
        float w = full * percent / 100.0f;
        if (charging) {
            float p = fmodf(time, 1.8f) / 1.8f;
            float sweep = w + (full - w) * smooth(p / 0.8f);
            float fade = 1 - smooth((p - 0.75f) / 0.25f);
            gfx_round_rect(x + 2.6f, y + 2.6f, sweep + 0.4f, 4.8f, 1, with_alpha(LIGHT, 0.5f * fade * a));
        }
        u32 c = percent <= 15 && !charging ? RGBA(255, 95, 85, 255) : LIGHT;
        gfx_round_rect(x + 2.6f, y + 2.6f, w + 0.4f, 4.8f, 1, with_alpha(c, a));
    }
    if (charging) {
        /* 1,4× större än förut: i den gamla storleken blev blixten bara några taggiga pixlar */
        const float s = 1.4f;
        float cx = x - 7, cy = y + 5;
        float bolt[12] = { cx + 1.6f * s, cy - 5.2f * s, cx - 2.6f * s, cy + 0.9f * s, cx - 0.2f * s, cy + 0.9f * s,
                           cx - 1.6f * s, cy + 5.2f * s, cx + 2.6f * s, cy - 0.9f * s, cx + 0.2f * s, cy - 0.9f * s };
        gfx_fan(cx, cy, bolt, 6, with_alpha(LIGHT, 0.9f * a));
    }
}

/* Egenritad not (två noter med balk), centrerad i (cx, cy), höjd ≈ size */
static void draw_note(float cx, float cy, float size, u32 color)
{
    float r = size * 0.2f, sw = fmaxf(size * 0.09f, 1.2f);
    float lx = cx - size * 0.28f, rx = cx + size * 0.32f;
    float ly = cy + size * 0.32f, ry = cy + size * 0.24f;
    float top_l = cy - size * 0.42f, top_r = cy - size * 0.5f;
    gfx_circle(lx, ly, r, color);
    gfx_circle(rx, ry, r, color);
    gfx_rect(lx + r - sw, top_l, sw, ly - top_l, color);
    gfx_rect(rx + r - sw, top_r, sw, ry - top_r, color);
    float beam = size * 0.17f;
    float xy[8] = { lx + r - sw, top_l, rx + r, top_r, lx + r - sw, top_l + beam, rx + r, top_r + beam };
    u32 c[4] = { color, color, color, color };
    gfx_quad(xy, c);
}

static void format_time(char *out, int size, float sec)
{
    int t = sec > 0 ? (int)sec : 0;
    snprintf(out, size, "%d:%02d", t / 60, t % 60);
}

static void draw_top_bar(Ui *ui)
{
    float a = ui->intro;

    ScePspDateTime now;
    char clock[8] = "";
    if (sceRtcGetCurrentClockLocalTime(&now) >= 0)
        snprintf(clock, sizeof(clock), "%02d:%02d", now.hour, now.minute);
    text_draw(SCREEN_W - MARGIN, 25, 0.8f, 0, with_alpha(TEXT_MAIN, a), ALIGN_RIGHT, clock);
    float cw = text_width(0.8f, 0, clock);
    float bx = SCREEN_W - MARGIN - cw - 32;
    int battery = scePowerIsBatteryExist();
    int charging = testrun_charging() >= 0 ? testrun_charging() : battery && scePowerIsBatteryCharging() > 0;
    int level = testrun_battery() >= 0 ? testrun_battery() : battery ? scePowerGetBatteryLifePercent() : -1;
    draw_battery(bx, 15.5f, level, charging, ui->time, a);

    /* flikarna krymper om de inte får plats (t.ex. långa ryska ord) – mäts bara om när något ändras */
    static float size = 0.8f, gap = 20, sized_room = -1;
    static int sized_tab = -1;
    static const char *sized_lang;
    float room = bx - 18 - MARGIN; /* plats för laddningsblixten till vänster om batteriet */
    if (room != sized_room || ui->tab != sized_tab || L(TAB_NAMES[0]) != sized_lang) {
        size = 0.8f;
        gap = 20;
        for (;;) {
            float w = 0;
            for (int t = 0; t < TAB_COUNT; t++)
                w += text_width(size, t == ui->tab, L(TAB_NAMES[t])) + gap;
            if (w - gap <= room || size <= 0.56f)
                break;
            size -= 0.04f;
            gap = 14;
        }
        sized_room = room;
        sized_tab = ui->tab;
        sized_lang = L(TAB_NAMES[0]);
    }
    float x = MARGIN;
    for (int t = 0; t < TAB_COUNT; t++) {
        int active = t == ui->tab;
        text_draw(x, 25, size, active, with_alpha(active ? TEXT_MAIN : RGBA(255, 255, 255, 120), a),
                  ALIGN_LEFT, L(TAB_NAMES[t]));
        x += text_width(size, active, L(TAB_NAMES[t])) + gap;
    }

    /* musik som spelas syns som en liten not med titeln i de andra flikarna */
    int cur = music_current();
    if (ui->tab != TAB_MUSIC && cur >= 0 && music_state() != MUSIC_STOPPED) {
        float right = bx - 16, left = x + 4;
        if (right - left > 40) {
            static char line[100];
            static int line_for = -2, line_gen = -1;
            static float line_w;
            const Track *tr = music_track(cur);
            if (tr && (line_for != cur || line_gen != music_generation() || line_w != right - left)) {
                text_ellipsis(line, sizeof(line), tr->title, 0.5f, right - left - 16);
                line_for = cur;
                line_gen = music_generation();
                line_w = right - left;
            }
            float pulse = music_state() == MUSIC_PLAYING ? 0.75f + 0.25f * sinf(ui->time * 3) : 0.5f;
            float tw = text_width(0.5f, 0, line);
            draw_note(right - tw - 8, 20.5f, 10, with_alpha(LIGHT, pulse * a));
            text_draw(right, 24.5f, 0.5f, 0, with_alpha(TEXT_DIM, a), ALIGN_RIGHT, line);
        }
    }
}

/* Placering för kortet på avståndet d från markeringen (0 = valt). */
static void card_geometry(float d, float *x, float *w, float *h, float *y)
{
    float t = clampf(fabsf(d), 0, 1);
    *w = lerpf(BIG_W, SMALL_W, t);
    *h = lerpf(BIG_H, SMALL_H, t);
    *y = lerpf(ROW_Y, ROW_Y + 3, t);
    if (d >= 1)
        *x = SMALL_X + (d - 1) * STEP;
    else if (d >= 0)
        *x = lerpf(MARGIN, SMALL_X, d);
    else if (d > -1)
        *x = lerpf(MARGIN, MARGIN - STEP, -d);
    else
        *x = MARGIN - STEP + (d + 1) * STEP;
}

static void draw_card(Ui *ui, int list_index, float x, float y, float w, float h, float focus, float alpha)
{
    IconSlot *slot = loader_icon(list_index);
    float r = lerpf(5.0f, BIG_R, (w - SMALL_W) / (BIG_W - SMALL_W));

    gfx_glow(x + 1, y + 3, w - 2, h - 2, 5 + 4 * focus, RGBA(0, 0, 0, (int)(130 * alpha)));

    float shown = 0;
    if (slot && slot->state == SLOT_READY) {
        slot->shown = clampf(slot->shown + 1.0f / 9.0f, 0, 1); /* ~150 ms intoning */
        shown = slot->shown;
    }
    if (shown < 1) {
        /* platshållare: mörkt kort med initial, andas svagt medan den laddas */
        int loading = !slot || slot->state == SLOT_EMPTY || slot->state == SLOT_LOADING;
        float b = loading ? 0.5f + 0.5f * sinf(ui->time * 4.0f + list_index) : 0;
        float pa = alpha * (1 - shown);
        u32 cc = theme_cur()->card; /* temats färg, andas medan bilden laddas */
        gfx_round_rect(x, y, w, h, r, RGBA((cc & 0xFF) + (int)(10 * b), ((cc >> 8) & 0xFF) + (int)(10 * b),
                                           ((cc >> 16) & 0xFF) + (int)(12 * b), (int)(235 * pa)));
        const char *t = ui->list->items[list_index].title;
        char initial[2] = { (t[0] >= 'a' && t[0] <= 'z') ? t[0] - 32 : t[0], 0 };
        if ((u8)initial[0] < 0x80 && initial[0])
            text_draw(x + w / 2, y + h / 2 + 8 * (w / BIG_W), 1.1f * (w / BIG_W), 0,
                      RGBA(205, 208, 220, (int)(200 * pa)), ALIGN_CENTER, initial);
    }
    if (shown > 0) {
        int tint = (int)lerpf(200, 255, focus);
        gfx_tex(slot->tex, x, y, w, h, RGBA(tint, tint, tint, (int)(255 * alpha * shown)));
    }

    /* valt kort: tunn ljus ram med ett litet glapp */
    if (focus > 0.01f)
        gfx_ring(x - 4, y - 4, w + 8, h + 8, r + 4, RGBA(240, 240, 244, (int)(235 * focus * alpha)));
}

static void draw_row(Ui *ui)
{
    const LoaderView *v = view(ui);
    float launch = ui->launch < 0 ? 0 : smooth(ui->launch);
    float row_alpha = ui->intro * smooth(ui->tab_anim);
    int first = (int)floorf(ui->pos) - 2, last = (int)floorf(ui->pos) + 8;
    if (first < 0) first = 0;
    if (last > v->count - 1) last = v->count - 1;

    /* närmast markeringen ritas sist, så att det stora kortet hamnar överst */
    int nearest = (int)floorf(ui->pos + 0.5f);
    for (int pass = 0; pass < 2; pass++) {
        for (int p = last; p >= first; p--) {
            if ((p == nearest) != (pass == 1))
                continue;
            float d = p - ui->pos;
            float x, y, w, h;
            card_geometry(d, &x, &w, &h, &y);
            float focus = 1 - clampf(fabsf(d), 0, 1);
            float alpha = row_alpha * (d < -0.5f ? clampf(1.5f + d, 0, 1) : 1); /* bleknar ut åt vänster */
            if (p == ui->sel[ui->tab] && launch > 0) {
                /* startanimation: kortet växer mot skärmens mitt */
                float grow = 1 + launch * 1.6f;
                float cx = lerpf(x + w / 2, SCREEN_W / 2, launch), cy = lerpf(y + h / 2, SCREEN_H / 2, launch);
                w *= grow;
                h *= grow;
                x = cx - w / 2;
                y = cy - h / 2;
            } else {
                alpha *= 1 - launch;
            }
            if (x > SCREEN_W || x + w < 0 || alpha <= 0.01f)
                continue;
            draw_card(ui, v->idx[p], x, y, w, h, focus, alpha);
        }
    }
}

static const char *type_name(u32 t)
{
    switch (t) {
    case ENTRY_ISO:      return "PSP";
    case ENTRY_HOMEBREW: return "APP";
    case ENTRY_PSN:      return "PSN";
    case ENTRY_POPS:     return "PS1";
    case ENTRY_UMD:      return "UMD";
    }
    return "";
}

static void draw_details(Ui *ui)
{
    int i = ui_selected(ui);
    float fade = ui->intro * smooth(ui->tab_anim) * (ui->launch < 0 ? 1 : 1 - smooth(ui->launch * 2));
    if (i < 0 || fade <= 0)
        return;
    const Entry *e = &ui->list->items[i];

    /* titeln bredvid kortet glider in när markeringen byts */
    float la = smooth(ui->label_anim);
    text_draw(SMALL_X + (1 - la) * 8, LABEL_Y, ui->label_size, 0, with_alpha(TEXT_MAIN, la * fade),
              ALIGN_LEFT, ui->label);

    /* nere till vänster: typetikett, spel-ID och startknapp */
    const char *type = type_name(e->type);
    float bw = text_width(0.5f, 1, type) + 12;
    float y = 196;
    gfx_round_rect(MARGIN, y, bw, 15, 3, with_alpha(LIGHT, 0.92f * fade));
    text_draw(MARGIN + bw / 2, y + 11, 0.5f, 1, with_alpha(DARK, fade), ALIGN_CENTER, type);
    if (e->type != ENTRY_HOMEBREW && e->game_id[0])
        text_draw(MARGIN + bw + 8, y + 11.5f, 0.62f, 0, with_alpha(TEXT_DIM, fade), ALIGN_LEFT, e->game_id);

    const char *go = L(S_LAUNCH);
    float bx = MARGIN, by = 220, bh = 26;
    float tw = text_width(0.66f, 0, go);
    float w = 8 + 16 + 8 + tw + 14;
    gfx_glow(bx, by + 2, w, bh, 6, RGBA(0, 0, 0, (int)(110 * fade)));
    gfx_round_rect(bx, by, w, bh, bh / 2, with_alpha(LIGHT, 0.95f * fade));
    gfx_button(bx + 8 + 8, by + bh / 2, 8, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, with_alpha(DARK, fade), with_alpha(LIGHT, fade));
    text_draw(bx + 32, by + 17.5f, 0.66f, 0, with_alpha(DARK, fade), ALIGN_LEFT, go);

    /* □ Alternativ (inte för UMD-skivan) */
    if (e->type == ENTRY_UMD)
        return;
    float oa = fade * (1 - smooth(ui->menu_anim));
    float mx = bx + w + 14, my = by + bh / 2;
    gfx_button(mx + 6.5f, my, 6.5f, BTN_SQUARE, with_alpha(LIGHT, 0.7f * oa), with_alpha(DARK, oa));
    text_draw(mx + 17, my + 4.5f, 0.56f, 0, with_alpha(TEXT_DIM, oa), ALIGN_LEFT, L(S_OPTIONS));
}

/* Alternativmenyn (□): en panel nere till vänster, över Starta-knappen, med kortets namn,
 * valet att flytta det till den andra fliken och ✕ / ○ längst ner. */
static void draw_menu(Ui *ui)
{
    float a = smooth(ui->menu_anim);
    int i = ui_selected(ui);
    if (a <= 0 || i < 0)
        return;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(110 * a)));
    float w = 214, h = 96, x = MARGIN - 6, y = 258 - h + (1 - a) * 10;
    gfx_glow(x, y, w, h, 10, RGBA(0, 0, 0, (int)(150 * a)));
    gfx_round_rect(x, y, w, h, 9, RGBA(40, 41, 47, (int)(255 * a)));

    char title[160];
    text_ellipsis(title, sizeof(title), ui->list->items[i].title, 0.5f, w - 24);
    text_draw(x + 12, y + 18, 0.5f, 0, with_alpha(TEXT_DIM, a), ALIGN_LEFT, title);

    const char *item = L(ui->tab == TAB_APPS ? S_MOVE_TO_GAMES : S_MOVE_TO_APPS);
    gfx_round_rect(x + 6, y + 26, w - 12, 30, 7, with_alpha(LIGHT, 0.95f * a));
    text_draw(x + 16, y + 46, text_fit(0.62f, 0.45f, 0, w - 32, item), 0, with_alpha(DARK, a), ALIGN_LEFT, item);

    float fx = x + 12, fy = y + h - 18;
    gfx_button(fx + 6.5f, fy, 6.5f, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, with_alpha(LIGHT, 0.9f * a),
               with_alpha(DARK, a));
    fx += 17;
    text_draw(fx, fy + 4.5f, 0.52f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, L(S_SELECT));
    fx += text_width(0.52f, 0, L(S_SELECT)) + 12;
    gfx_button(fx + 6.5f, fy, 6.5f, ui->confirm_circle ? BTN_CROSS : BTN_CIRCLE, with_alpha(LIGHT, 0.9f * a),
               with_alpha(DARK, a));
    fx += 17;
    text_draw(fx, fy + 4.5f, 0.52f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, L(S_BACK));
}

static float key_cap(float x, float y, const char *key, float a)
{
    float kw = text_width(0.42f, 1, key) + 9;
    gfx_round_rect(x, y - 6.5f, kw, 13, 3, with_alpha(LIGHT, 0.9f * a));
    text_draw(x + kw / 2, y + 3.5f, 0.42f, 1, with_alpha(DARK, a), ALIGN_CENTER, key);
    return kw;
}

static float hints_width(void);

static void draw_hints(Ui *ui)
{
    float a = ui->intro * (ui->launch < 0 ? 1 : 1 - smooth(ui->launch * 2));
    const char *tab = L(S_HINT_TAB), *opts = L(S_SETTINGS);
    float w = hints_width();
    float x = SCREEN_W - MARGIN - w, y = 246;

    gfx_round_rect(x - 9, y - 11, w + 18, 22, 11, RGBA(0, 0, 0, (int)(120 * a)));
    x += key_cap(x, y, "L", a) + 3;
    x += key_cap(x, y, "R", a) + 6;
    text_draw(x, y + 4.5f, 0.6f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, tab);
    x += text_width(0.6f, 0, tab) + 16;
    gfx_button(x + 6.5f, y, 6.5f, BTN_TRIANGLE, with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    x += 13 + 6;
    text_draw(x, y + 4.5f, 0.6f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, opts);
}

static float hints_width(void)
{
    static float w;
    static const char *for_lang; /* bredden ändras bara med språket */
    if (for_lang != L(S_SETTINGS)) {
        float lw = text_width(0.42f, 1, "L") + 9, rw = text_width(0.42f, 1, "R") + 9;
        w = lw + 3 + rw + 6 + text_width(0.6f, 0, L(S_HINT_TAB)) + 16 + 13 + 6 + text_width(0.6f, 0, L(S_SETTINGS));
        for_lang = L(S_SETTINGS);
    }
    return w;
}

/* Biblioteket: allt i ett rutnät, rubrik med antal och sortering, vald titel nere till vänster. */
static void draw_library(Ui *ui)
{
    const LoaderView *v = view(ui);
    float launch = ui->launch < 0 ? 0 : smooth(ui->launch);
    float a = ui->intro * smooth(ui->tab_anim);
    float ui_a = a * (1 - smooth(launch * 2));
    float slide = (1 - smooth(ui->tab_anim)) * 10;

    /* mörkare yta än i raden – valt spels bild skymtar bara svagt */
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(8, 8, 12, 165));

    /* rubrik: antal till vänster, sortering till höger */
    char head[96];
    snprintf(head, sizeof(head), "%s", L(S_LIB_ALL));
    text_draw(MARGIN, 53, 0.66f, 0, with_alpha(TEXT_MAIN, ui_a), ALIGN_LEFT, head);
    char num[16];
    snprintf(num, sizeof(num), "%d", v->count);
    float hw = text_width(0.66f, 0, head);
    text_draw(MARGIN + hw + 8, 53, 0.6f, 0, with_alpha(TEXT_DIM, ui_a), ALIGN_LEFT, num);

    char sort[64];
    snprintf(sort, sizeof(sort), "%s: %s", L(S_SORT), L(ui->lib_sort ? S_SORT_RECENT : S_SORT_NAME));
    text_draw(SCREEN_W - MARGIN, 53, 0.56f, 0, with_alpha(TEXT_DIM, ui_a), ALIGN_RIGHT, sort);
    gfx_button(SCREEN_W - MARGIN - text_width(0.56f, 0, sort) - 11, 49, 6, BTN_SQUARE,
               with_alpha(LIGHT, 0.85f * ui_a), with_alpha(DARK, ui_a));

    /* korten – klipps under rubriken */
    int sel = ui->sel[TAB_LIBRARY];
    float focus_anim = smooth(ui->label_anim);
    int first_row = (int)floorf(ui->grid_scroll) - 1, last_row = (int)ceilf(ui->grid_scroll) + GRID_ROWS + 1;
    gfx_clip(0, GRID_Y - 8, SCREEN_W, GRID_BOTTOM - (GRID_Y - 8));
    float sx = 0, sy = 0, sw = 0, shh = 0;
    for (int p = first_row * GRID_COLS; p < (last_row + 1) * GRID_COLS && p < v->count; p++) {
        if (p < 0)
            continue;
        int r = p / GRID_COLS, c = p % GRID_COLS;
        float x = MARGIN + c * (CELL_W + GRID_GAP);
        float y = GRID_Y + (r - ui->grid_scroll) * ROW_STEP + slide;
        if (y > SCREEN_H || y + CELL_H < GRID_Y - 8)
            continue;
        if (p == sel) { /* ritas sist, ovanpå grannarna */
            sx = x; sy = y; sw = CELL_W; shh = CELL_H;
            continue;
        }
        /* en rad som bara syns delvis bleknar */
        float fade = clampf((GRID_BOTTOM - y) / CELL_H, 0, 1) * clampf((y + CELL_H - GRID_Y + 8) / CELL_H, 0, 1);
        draw_card(ui, v->idx[p], x, y, CELL_W, CELL_H, 0, a * (1 - launch) * fade);
    }
    if (sw > 0) {
        float grow = 1 + 0.08f * focus_anim;
        float w = sw * grow, h = shh * grow;
        float cx = sx + sw / 2, cy = sy + shh / 2;
        if (launch > 0) { /* startanimation: kortet växer mot mitten */
            gfx_clip_reset();
            float g = 1 + launch * 2.4f;
            w *= g;
            h *= g;
            cx = lerpf(cx, SCREEN_W / 2, launch);
            cy = lerpf(cy, SCREEN_H / 2, launch);
        }
        draw_card(ui, v->idx[sel], cx - w / 2, cy - h / 2, w, h, 0.4f + 0.6f * focus_anim, a);
    }
    gfx_clip_reset();

    /* vald titel med typetikett nere till vänster, bredvid knapptipsen */
    int i = ui_selected(ui);
    if (i >= 0) {
        const char *type = type_name(ui->list->items[i].type);
        float bw = text_width(0.5f, 1, type) + 12;
        gfx_round_rect(MARGIN, 238.5f, bw, 15, 3, with_alpha(LIGHT, 0.92f * ui_a));
        text_draw(MARGIN + bw / 2, 249.5f, 0.5f, 1, with_alpha(DARK, ui_a), ALIGN_CENTER, type);
        float tx = MARGIN + bw + 8;
        float max_w = SCREEN_W - MARGIN - hints_width() - 30 - tx;
        if (ui->lib_label_for != i) {
            text_ellipsis(ui->lib_label, sizeof(ui->lib_label), ui->list->items[i].title, 0.66f, max_w);
            ui->lib_label_for = i;
        }
        text_draw(tx + (1 - focus_anim) * 6, 250.5f, 0.66f, 0, with_alpha(TEXT_MAIN, ui_a * focus_anim),
                  ALIGN_LEFT, ui->lib_label);
    }
}

/* Musikfliken: låtlista till vänster, "spelas nu" med förlopp till höger, knapptips nere till vänster */
typedef struct { int idx; char title[100]; char sub[72]; } RowCache;

static void draw_play_glyph(float cx, float cy, float r, int paused, u32 color)
{
    if (paused) {
        gfx_rect(cx - r * 0.55f, cy - r * 0.6f, r * 0.38f, r * 1.2f, color);
        gfx_rect(cx + r * 0.17f, cy - r * 0.6f, r * 0.38f, r * 1.2f, color);
    } else {
        float xy[6] = { cx - r * 0.42f, cy - r * 0.62f, cx + r * 0.68f, cy, cx - r * 0.42f, cy + r * 0.62f };
        gfx_fan(cx, cy, xy, 3, color);
    }
}

/* Spolning: två trianglar åt hållet man spolar (▶▶ eller ◀◀) */
static void draw_seek_glyph(float cx, float cy, float r, int dir, u32 color)
{
    for (int k = 0; k < 2; k++) {
        float tip = cx + dir * (k ? r : 0), back = tip - dir * r;
        float xy[6] = { back, cy - r * 0.7f, tip, cy, back, cy + r * 0.7f };
        gfx_fan((back + tip) / 2, cy, xy, 3, color);
    }
}

/* Tipstexterna säger ms0:/ – på PSP Go:s internminne visas ef0:/ i stället */
static const char *on_root(const char *s)
{
    static char buf[160];
    const char *p = strstr(s, "ms0:");
    if (!p || !strcmp(g_root, "ms0:"))
        return s;
    snprintf(buf, sizeof(buf), "%.*s%s%s", (int)(p - s), s, g_root, p + 4);
    return buf;
}

static void draw_music(Ui *ui)
{
    float a = ui->intro * smooth(ui->tab_anim);
    float slide = (1 - smooth(ui->tab_anim)) * 10;
    int n = music_count(), cur = music_current(), state = music_state();
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(8, 8, 12, 150));

    /* --- spelas nu --- */
    float px = PANEL_X, pw = SCREEN_W - MARGIN - PANEL_X, py = MLIST_Y + slide, ph = MROWS * MROW_H - 2;
    gfx_round_rect(px, py, pw, ph, 10, RGBA(255, 255, 255, (int)(16 * a)));
    text_draw(px + 12, py + 17, 0.5f, 0, with_alpha(TEXT_DIM, a), ALIGN_LEFT, L(S_NOW_PLAYING));
    const Track *t = cur >= 0 ? music_track(cur) : NULL;
    /* omslaget, eller en ruta med färgton från låtens nummer och en egenritad not */
    float as = 64, ax = px + (pw - as) / 2, ay = py + 28;
    unsigned h = (cur >= 0 ? cur : 7) * 2654435761u;
    int r0 = 60 + (h >> 8) % 120, g0 = 60 + (h >> 16) % 110, b0 = 90 + (h >> 24) % 140;
    gfx_glow(ax, ay + 3, as, as, 8, RGBA(0, 0, 0, (int)(120 * a)));
    gfx_round_rect(ax, ay, as, as, 8, RGBA(r0, g0, b0, (int)((t ? 255 : 90) * a)));
    gfx_gradient_v(ax + 4, ay + as * 0.5f, as - 8, as * 0.5f - 4, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, (int)(60 * a)));
    draw_note(ax + as / 2, ay + as / 2, 30, RGBA(255, 255, 255, (int)((t ? 230 : 120) * a)));
    if (ui->cover && ui->cover_for == cur && cur >= 0) {
        float ca = smooth(ui->cover_fade);
        gfx_tex(ui->cover, ax, ay, as, as, RGBA(255, 255, 255, (int)(255 * a * ca)));
    }

    static char ptitle[100], partist[72];
    static int pfor = -2, pgen = -1;
    static const char *plang;
    if (pfor != cur || pgen != music_generation() || plang != L(S_UNKNOWN_ARTIST)) {
        pgen = music_generation();
        plang = L(S_UNKNOWN_ARTIST);
        text_ellipsis(ptitle, sizeof(ptitle), t ? t->title : L(S_NOTHING_PLAYING), 0.6f, pw - 20);
        text_ellipsis(partist, sizeof(partist), t ? (t->artist[0] ? t->artist : L(S_UNKNOWN_ARTIST)) : "", 0.5f, pw - 20);
        pfor = cur;
    }
    text_draw(px + pw / 2, ay + as + 20, 0.6f, 0, with_alpha(TEXT_MAIN, a), ALIGN_CENTER, ptitle);
    text_draw(px + pw / 2, ay + as + 35, 0.5f, 0, with_alpha(TEXT_DIM, a), ALIGN_CENTER, partist);
    if (t) {
        int seek = ui->seeking || ui->seek_show > 0;
        float el = seek ? ui->seek_pos : music_elapsed(), du = music_duration();
        float bw = pw - 28, bxx = px + 14, by = py + ph - 30;
        gfx_round_rect(bxx, by, bw, 3, 1.5f, RGBA(255, 255, 255, (int)(50 * a)));
        if (du > 0)
            gfx_round_rect(bxx, by, fmaxf(3, bw * clampf(el / du, 0, 1)), 3, 1.5f, with_alpha(theme_cur()->accent, a));
        if (du > 0 && ui->seeking) /* spolning: en knopp på målet */
            gfx_circle(bxx + bw * clampf(el / du, 0, 1), by + 1.5f, 4.5f, with_alpha(LIGHT, a));
        char e[12], d[12];
        format_time(e, sizeof(e), el);
        format_time(d, sizeof(d), du);
        text_draw(bxx, by + 15, 0.45f, 0, with_alpha(TEXT_DIM, a), ALIGN_LEFT, e);
        if (du > 0)
            text_draw(bxx + bw, by + 15, 0.45f, 0, with_alpha(TEXT_DIM, a), ALIGN_RIGHT, d);
        gfx_circle(px + pw / 2, by + 11, 8, with_alpha(LIGHT, 0.95f * a));
        if (ui->seeking)
            draw_seek_glyph(px + pw / 2, by + 11, 5.5f, ui->seek_dir, with_alpha(DARK, a));
        else
            draw_play_glyph(px + pw / 2, by + 11, 6, state == MUSIC_PLAYING, with_alpha(DARK, a));
    }

    if (!n && music_scanning()) {
        text_draw(MARGIN, 120, 0.9f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, L(S_LOADING_MUSIC));
        for (int i = 0; i < 3; i++) { /* tre punkter som pulserar i tur och ordning */
            float p = 0.5f + 0.5f * sinf(ui->time * 6.0f - i * 0.9f);
            gfx_circle(MARGIN + 4 + i * 12, 140, 2 + p, COLOR_ALPHA(theme_cur()->accent, (int)((90 + 165 * p) * a)));
        }
        return;
    }
    if (!n) {
        text_draw(MARGIN, 120, 0.9f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, L(S_NO_MUSIC));
        text_draw(MARGIN, 142, 0.62f, 0, with_alpha(TEXT_DIM, a), ALIGN_LEFT, on_root(L(S_HINT_MUSIC)));
        return;
    }

    /* --- låtlistan --- */
    static RowCache cache[MROWS + 3];
    static int cache_gen = -1;
    static const char *cache_lang;
    if (cache_gen != music_generation() || cache_lang != L(S_UNKNOWN_ARTIST)) { /* ny lista eller nytt språk */
        for (int i = 0; i < MROWS + 3; i++)
            cache[i].idx = -1;
        cache_gen = music_generation();
        cache_lang = L(S_UNKNOWN_ARTIST);
    }
    float lw = PANEL_X - 14 - MARGIN;
    gfx_clip(0, MLIST_Y - 2, PANEL_X - 6, MROWS * MROW_H + 2);
    int first = (int)floorf(ui->music_scroll), last = first + MROWS;
    for (int i = first; i <= last && i < n; i++) {
        if (i < 0)
            continue;
        float y = MLIST_Y + (i - ui->music_scroll) * MROW_H + slide;
        RowCache *c = &cache[i % (MROWS + 3)];
        const Track *tr = music_track(i);
        if (!tr)
            continue;
        if (c->idx != i) {
            text_ellipsis(c->title, sizeof(c->title), tr->title, 0.6f, lw - 30);
            text_ellipsis(c->sub, sizeof(c->sub), tr->artist[0] ? tr->artist : L(S_UNKNOWN_ARTIST), 0.46f, lw - 30);
            c->idx = i;
        }
        int selected = i == ui->music_sel;
        if (selected)
            gfx_round_rect(MARGIN - 6, y, lw + 12, MROW_H - 2, 7, with_alpha(LIGHT, 0.93f * a));
        u32 main = selected ? with_alpha(DARK, a) : with_alpha(TEXT_MAIN, a);
        u32 dim = selected ? RGBA(70, 72, 80, (int)(255 * a)) : with_alpha(TEXT_DIM, a);
        if (i == cur && state != MUSIC_STOPPED) {
            /* små staplar som rör sig medan låten spelas */
            for (int k = 0; k < 3; k++) {
                float hgt = state == MUSIC_PLAYING ? 3 + 7 * (0.5f + 0.5f * sinf(ui->time * (7 + k * 2.3f) + k)) : 3;
                gfx_rect(MARGIN + 2 + k * 4, y + 18 - hgt, 2.5f, hgt, main);
            }
        }
        text_draw(MARGIN + 20, y + 11.5f, 0.6f, 0, main, ALIGN_LEFT, c->title);
        text_draw(MARGIN + 20, y + 22, 0.46f, 0, dim, ALIGN_LEFT, c->sub);
    }
    gfx_clip_reset();

    /* knapptips nere till vänster: ✕ Spela  □ Paus  ◀ ▶ Spår */
    float x = MARGIN, y = 246;
    gfx_button(x + 6.5f, y, 6.5f, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    x += 17;
    text_draw(x, y + 4.5f, 0.56f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, L(S_PLAY));
    x += text_width(0.56f, 0, L(S_PLAY)) + 12;
    gfx_button(x + 6.5f, y, 6.5f, BTN_SQUARE, with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    x += 17;
    text_draw(x, y + 4.5f, 0.56f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, L(S_PAUSE));
    x += text_width(0.56f, 0, L(S_PAUSE)) + 12;
    float l[6] = { x, y, x + 6, y - 4.5f, x + 6, y + 4.5f };
    gfx_fan(x + 3, y, l, 3, with_alpha(LIGHT, 0.9f * a));
    float r[6] = { x + 15, y, x + 9, y - 4.5f, x + 9, y + 4.5f };
    gfx_fan(x + 12, y, r, 3, with_alpha(LIGHT, 0.9f * a));
    /* krymper hellre än krockar med L R-tipsen till höger */
    float tsize = text_fit(0.56f, 0.4f, 0, SCREEN_W - MARGIN - hints_width() - 26 - (x + 20), L(S_TRACK));
    text_draw(x + 20, y + 4.5f, tsize, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, L(S_TRACK));
}

static void draw_toast(Ui *ui)
{
    if (ui->toast_time <= 0)
        return;
    float a = clampf(ui->toast_time / 0.4f, 0, 1) * clampf((2.8f - ui->toast_time) / 0.2f, 0, 1);
    float w = text_width(0.62f, 0, ui->toast) + 32;
    float x = (SCREEN_W - w) / 2, y = 132;
    gfx_glow(x, y, w, 28, 10, RGBA(0, 0, 0, (int)(150 * a)));
    gfx_round_rect(x, y, w, 28, 7, RGBA(40, 41, 47, (int)(245 * a)));
    text_draw(SCREEN_W / 2, y + 18.5f, 0.62f, 0, RGBA(240, 240, 245, (int)(255 * a)), ALIGN_CENTER, ui->toast);
}

static void draw_empty(Ui *ui)
{
    float a = ui->intro * smooth(ui->tab_anim);
    const char *title = L(ui->tab == TAB_APPS ? S_NO_APPS : S_NO_GAMES);
    const char *hint = on_root(L(ui->tab == TAB_APPS ? S_HINT_APPS : S_HINT_GAMES));
    text_draw(MARGIN, 120, 0.9f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, title);
    text_draw(MARGIN, 142, 0.62f, 0, with_alpha(TEXT_DIM, a), ALIGN_LEFT, hint);
}

void ui_draw(Ui *ui)
{
    gfx_begin(CLEAR_COLOR);
    draw_background(ui);
    draw_top_bar(ui);
    if (ui->tab == TAB_MUSIC) {
        draw_music(ui);
    } else if (view(ui)->count && ui->tab == TAB_LIBRARY) {
        draw_library(ui);
    } else if (view(ui)->count) {
        draw_row(ui);
        draw_details(ui);
    } else {
        draw_empty(ui);
    }
    draw_hints(ui);
    if (ui->tab == TAB_GAMES || ui->tab == TAB_APPS)
        draw_menu(ui);
    draw_toast(ui);
    osd_draw();

    /* in- och uttoning från/till svart */
    float black = 1 - ui->intro;
    if (ui->launch > 0)
        black = fmaxf(black, clampf((ui->launch - 0.35f) / 0.65f, 0, 1));
    if (black > 0)
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(255 * black)));
    gfx_end();
}

void ui_draw_scanning(int done, const char *name, float time)
{
    static const float neutral[3] = { 120, 120, 120 };
    gfx_begin(CLEAR_COLOR);
    draw_spotlight(neutral, time);
    text_draw(MARGIN, 120, 0.9f, 0, TEXT_MAIN, ALIGN_LEFT, L(S_LOADING));

    /* tre punkter som pulserar i tur och ordning */
    for (int i = 0; i < 3; i++) {
        float p = 0.5f + 0.5f * sinf(time * 6.0f - i * 0.9f);
        gfx_circle(MARGIN + 4 + i * 12, 140, 2 + p, COLOR_ALPHA(theme_cur()->accent, (int)(90 + 165 * p)));
    }
    char line[96];
    snprintf(line, sizeof(line), "%d  ·  %.60s", done, name);
    text_draw(MARGIN, 170, 0.6f, 0, TEXT_DIM, ALIGN_LEFT, line);
    gfx_end();
}
