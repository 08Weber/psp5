/* psp5 – made by Mille Weber */
/* psp5:s utseende: valt spels bild över hela skärmen, en rad kort högst upp
 * där valt kort är stort med tunn ljus ram, flikar för spel/appar/bibliotek
 * (biblioteket visar allt i ett rutnät),
 * neutral ljus typografi och mjuka animationer. All grafik ritas här. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <psppower.h>
#include <psprtc.h>
#include <psputility.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ui.h"
#include "art.h"
#include "config.h"
#include "preview.h"
#include "sound.h"
#include "lang.h"
#include "music.h"
#include "system.h"
#include "theme.h"
#include "osd.h"
#include "testrun.h"
#include "settings.h" /* SA_* för Control Center */

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
#define PREVIEW_DELAY 0.9f /* stå still så här länge innan spelets video och musik startar */
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

/* Homebrew hamnar under Appar – utom i en kategorimapp med prefixet g_ (spel, t.ex. GBA-spel
 * som EBOOT) – och s_ lägger allt under Appar. □ → Flytta byter sedan för ett enskilt kort. */
static int is_app(const Entry *e)
{
    int app = e->type == ENTRY_HOMEBREW;
    if (!strncasecmp(e->category, "g_", 2))
        app = 0;
    else if (!strncasecmp(e->category, "s_", 2))
        app = 1;
    return app != (moved_find(e) != NULL);
}

/* --- kategorier --- */

#define MAX_CATS 64
static char s_cat_name[MAX_CATS][40]; /* mappnamnet, t.ex. "g_GBA" */
static char s_cat_dir[MAX_CATS][96];  /* mappen (för bg.png), t.ex. "ms0:/PSP/GAME/g_GBA" */
static int s_ncat;
static int s_cat_n[TAB_COUNT][MAX_CATS + 1]; /* antal kort per flik och kategori, [t][0] = alla */
static int s_pick[MAX_CATS + 1], s_npick;    /* raderna i väljaren: 0 = alla, annars kategori */

/* 1-baserat index för postens kategori, 0 = ingen */
static int cat_of(const Entry *e)
{
    for (int c = 0; e->category[0] && c < s_ncat; c++)
        if (!strcasecmp(s_cat_name[c], e->category))
            return c + 1;
    return 0;
}

static void collect_cats(const EntryList *l)
{
    s_ncat = 0;
    for (int i = 0; i < l->count; i++) {
        const Entry *e = &l->items[i];
        if (!e->category[0] || cat_of(e) || s_ncat >= MAX_CATS)
            continue;
        snprintf(s_cat_name[s_ncat], sizeof(s_cat_name[0]), "%s", e->category);
        /* mappen = sökvägen fram till "/<kategori>/" */
        char key[44];
        snprintf(key, sizeof(key), "/%s/", e->category);
        const char *p = strstr(e->path, key);
        snprintf(s_cat_dir[s_ncat], sizeof(s_cat_dir[0]), "%.*s", p ? (int)(p - e->path) + (int)strlen(key) - 1 : 0,
                 e->path);
        s_ncat++;
    }
}

/* Visningsnamn: utan prefixen g_, s_ och CAT_ (Game Categories), _ blir mellanslag */
static const char *cat_title(int c)
{
    static char buf[40];
    if (c <= 0 || c > s_ncat)
        return L(S_ALL);
    const char *s = s_cat_name[c - 1];
    if (!strncasecmp(s, "CAT_", 4))
        s += 4;
    else if (!strncasecmp(s, "g_", 2) || !strncasecmp(s, "s_", 2))
        s += 2;
    snprintf(buf, sizeof(buf), "%s", s);
    for (char *p = buf; *p; p++)
        if (*p == '_')
            *p = ' ';
    return buf;
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

static Texture *s_wall;   /* egen bakgrundsbild (Inställningar → Tema), NULL = temats */
static char s_wall_for[sizeof(g_wallpaper)] = "\1"; /* filen s_wall laddades från, "\1" = ingen */

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

/* Samma spel en gång till (samma spel-ID och titel, t.ex. både ISO och CSO, eller i två
 * mappar)? Listan är sorterad på titel, så kopiorna ligger intill varandra. UMD-skivan
 * räknas inte – den ska synas även om spelet också finns som ISO. */
static int is_dupe(const EntryList *l, int i)
{
    const Entry *e = &l->items[i];
    if (!g_hide_dupes || e->type == ENTRY_UMD || !e->game_id[0])
        return 0;
    for (int j = i - 1; j >= 0 && !strcasecmp(l->items[j].title, e->title); j--)
        if (l->items[j].type != ENTRY_UMD && !strcmp(l->items[j].title, e->title) &&
            !strcmp(l->items[j].game_id, e->game_id))
            return 1;
    return 0;
}

static void build_views(Ui *ui)
{
    collect_cats(ui->list);
    memset(s_cat_n, 0, sizeof(s_cat_n));
    for (int i = 0; i < ui->list->count; i++) {
        if (is_dupe(ui->list, i))
            continue;
        int c = cat_of(&ui->list->items[i]);
        s_cat_n[is_app(&ui->list->items[i]) ? TAB_APPS : TAB_GAMES][c]++;
        s_cat_n[TAB_LIBRARY][c]++;
    }
    for (int t = 0; t < TAB_COUNT; t++) {
        ui->views[t].idx = malloc((ui->list->count ? ui->list->count : 1) * sizeof(int));
        ui->views[t].count = 0;
        if (ui->cat[t] < 0 || ui->cat[t] > s_ncat || !s_cat_n[t][ui->cat[t]])
            ui->cat[t] = 0; /* kategorin är tom eller borta (t.ex. sista kortet flyttat) */
    }
    for (int i = 0; i < ui->list->count; i++) {
        if (is_dupe(ui->list, i))
            continue;
        const Entry *e = &ui->list->items[i];
        int c = cat_of(e);
        int tab = is_app(e) ? TAB_APPS : TAB_GAMES;
        LoaderView *v = &ui->views[tab];
        /* UMD-skivan syns i alla kategorier */
        if (v->idx && (!ui->cat[tab] || c == ui->cat[tab] || e->type == ENTRY_UMD))
            v->idx[v->count++] = i;
        LoaderView *lib = &ui->views[TAB_LIBRARY];
        if (lib->idx && (!ui->cat[TAB_LIBRARY] || c == ui->cat[TAB_LIBRARY] || e->type == ENTRY_UMD))
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

/* <data>/last.txt: fliken, sedan för Spel, Appar och Bibliotek kategorins mappnamn och valt
 * korts nyckel (entry_cover_key) – en rad var. Efter ett spel öppnar psp5 där man var. */
static void last_path(char *out, int size)
{
    snprintf(out, size, "%s/last.txt", g_data_dir);
}

static void last_save(const Ui *ui)
{
    char buf[512], key[80], path[64];
    int n = snprintf(buf, sizeof(buf), "%d\n", ui->tab);
    for (int t = 0; t < TAB_MUSIC; t++) {
        const LoaderView *v = &ui->views[t];
        key[0] = 0;
        if (v->idx && ui->sel[t] < v->count)
            entry_cover_key(&ui->list->items[v->idx[ui->sel[t]]], key, sizeof(key));
        n += snprintf(buf + n, sizeof(buf) - n, "%s\n%s\n", ui->cat[t] > 0 ? s_cat_name[ui->cat[t] - 1] : "", key);
    }
    last_path(path, sizeof(path));
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, buf, n);
        sceIoClose(fd);
    }
}

/* Läser flik och kategorier till ui och kortens nycklar till keys. 0 = ingen fil. */
static int last_load(Ui *ui, char keys[TAB_MUSIC][80])
{
    char buf[512], path[64], *line[1 + 2 * TAB_MUSIC];
    last_path(path, sizeof(path));
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    int n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    buf[n > 0 ? n : 0] = 0;
    int nl = 0;
    for (char *p = buf; nl < 1 + 2 * TAB_MUSIC;) {
        line[nl++] = p;
        char *end = strchr(p, '\n');
        if (!end)
            break;
        *end = 0;
        p = end + 1;
    }
    if (nl < 1 + 2 * TAB_MUSIC)
        return 0;
    ui->tab = clampf(atoi(line[0]), 0, TAB_COUNT - 1);
    for (int t = 0; t < TAB_MUSIC; t++) {
        for (int c = 0; c < s_ncat; c++)
            if (!strcasecmp(s_cat_name[c], line[1 + 2 * t]))
                ui->cat[t] = c + 1;
        snprintf(keys[t], 80, "%s", line[2 + 2 * t]);
    }
    return 1;
}

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
    ui->cat_bg_for = -1;
    moved_load();
    collect_cats(list); /* last_load letar kategorierna med namn */
    char keys[TAB_MUSIC][80] = { "" };
    int had_last = last_load(ui, keys);
    build_views(ui);
    if (!had_last)
        ui->tab = ui->views[TAB_GAMES].count || !ui->views[TAB_APPS].count ? TAB_GAMES : TAB_APPS;
    char key[80];
    for (int t = 0; t < TAB_MUSIC; t++)
        for (int p = 0; keys[t][0] && p < ui->views[t].count; p++) {
            entry_cover_key(&list->items[ui->views[t].idx[p]], key, sizeof(key));
            if (!strcmp(key, keys[t])) {
                ui->sel[t] = p;
                break;
            }
        }
    ui->pos = (ui->tab == TAB_MUSIC ? 0 : ui->sel[ui->tab]) + 1.5f; /* korten glider in vid start */
    ui->grid_target = ui->sel[TAB_LIBRARY] / GRID_COLS;
    ui->grid_scroll = ui->grid_target;
    loader_set_view(&ui->views[ui->tab == TAB_MUSIC ? TAB_GAMES : ui->tab]);
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
    if (ui->list)
        last_save(ui);
    tex_free(ui->bg_old);
    tex_free(ui->bg_cur);
    ui->bg_old = ui->bg_cur = NULL;
    tex_free(ui->logo_cur);
    ui->logo_cur = NULL;
    tex_free(ui->cat_bg);
    ui->cat_bg = NULL;
    ui->cat_bg_for = -1;
    tex_free(ui->cover);
    ui->cover = NULL;
    tex_free(s_wall);
    s_wall = NULL;
    strcpy(s_wall_for, "\1");
    for (int t = 0; t < TAB_COUNT; t++) {
        free(ui->views[t].idx);
        ui->views[t].idx = NULL;
        ui->views[t].count = 0;
    }
    ui->list = NULL; /* last.txt sparas bara en gång */
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
    Texture *tex, *logo;
    if (loader_take_bg(&idx, &tex, &logo)) {
        if (idx == sel && idx != ui->bg_index) {
            tex_free(ui->bg_old);
            tex_free(ui->logo_cur); /* loggan tonas bara in – den gamla hör till ett annat kort */
            ui->bg_old = ui->bg_cur;
            ui->bg_cur = tex;
            ui->logo_cur = logo;
            ui->bg_index = idx;
            ui->bg_fade = 0;
        } else {
            tex_free(tex); /* hann flytta vidare */
            tex_free(logo);
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
    if (sel < 0 && ui->logo_cur) {
        tex_free(ui->logo_cur);
        ui->logo_cur = NULL;
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
    if (in->pressed & PSP_CTRL_SELECT) { /* blanda – gäller från nästa låt */
        g_shuffle ^= 1;
        config_save(g_data_dir);
        ui_toast(ui, L(g_shuffle ? S_SHUFFLE_ON : S_SHUFFLE_OFF));
        sound_play(SND_TOGGLE);
    }
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

/* --- kategoriväljaren (SELECT) --- */

#define CAT_ROWS 7
#define CAT_ROW_H 26.0f

static void open_categories(Ui *ui)
{
    s_npick = 0;
    for (int c = 0; c <= s_ncat; c++)
        if (!c || s_cat_n[ui->tab][c])
            s_pick[s_npick++] = c;
    if (s_npick < 2) {
        ui_toast(ui, L(S_NO_CATEGORIES));
        sound_play(SND_ERROR);
        return;
    }
    ui->cat_open = 1;
    ui->cat_sel = 0;
    for (int i = 0; i < s_npick; i++)
        if (s_pick[i] == ui->cat[ui->tab])
            ui->cat_sel = i;
    ui->cat_scroll = ui->cat_sel >= CAT_ROWS ? ui->cat_sel - CAT_ROWS + 1 : 0;
    ui->cat_still = 0;
    sound_play(SND_OPEN);
}

static void update_categories(Ui *ui, const UiInput *in, float dt)
{
    ui->cat_anim = clampf(ui->cat_anim + (ui->cat_open ? 7 : -7) * dt, 0, 1);
    if (!ui->cat_open) {
        if (ui->cat_anim <= 0 && ui->cat_bg) { /* stängd: släpp bilden */
            tex_free(ui->cat_bg);
            ui->cat_bg = NULL;
            ui->cat_bg_for = -1;
        }
        return;
    }
    int move = step_input(ui, in, dt, 1);
    if (move == 2 || move == -2) {
        int next = clampf(ui->cat_sel + move / 2, 0, s_npick - 1);
        if (next != ui->cat_sel) {
            ui->cat_sel = next;
            ui->cat_still = 0;
            sound_play(SND_MOVE);
        }
    }
    if (ui->cat_sel < ui->cat_scroll)
        ui->cat_scroll = ui->cat_sel;
    if (ui->cat_sel > ui->cat_scroll + CAT_ROWS - 1)
        ui->cat_scroll = ui->cat_sel - (CAT_ROWS - 1);

    /* mappens bg.png – när markeringen har stått still en stund (laddas sedan från cachen) */
    ui->cat_still += dt;
    if (ui->cat_still >= 0.2f && ui->cat_bg_for != ui->cat_sel) {
        tex_free(ui->cat_bg);
        ui->cat_bg = NULL;
        int c = s_pick[ui->cat_sel];
        if (c > 0 && s_cat_dir[c - 1][0]) {
            char path[sizeof(s_cat_dir[0]) + 8];
            snprintf(path, sizeof(path), "%s/bg.png", s_cat_dir[c - 1]);
            ui->cat_bg = art_load_wallpaper(path);
        }
        ui->cat_bg_for = ui->cat_sel;
        ui->cat_bg_fade = 0;
    }
    ui->cat_bg_fade = clampf(ui->cat_bg_fade + dt * 4, 0, 1);

    u32 ok = ui->confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
    u32 back = (ui->confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE) | PSP_CTRL_SELECT;
    if (in->pressed & ok) {
        ui->cat_open = 0;
        if (s_pick[ui->cat_sel] != ui->cat[ui->tab]) {
            ui->cat[ui->tab] = s_pick[ui->cat_sel];
            ui_relist(ui, 0, 0); /* markeringen står kvar om kortet finns i kategorin */
            ui->pos = ui->sel[ui->tab] + 0.7f;
            ui->tab_anim = 0;
        }
        sound_play(SND_SELECT);
    } else if (in->pressed & back) {
        ui->cat_open = 0;
        sound_play(SND_BACK);
    }
}

/* --- Control Center (HOME) --- */

enum { CC_MUSIC, CC_SLEEP, CC_USB, CC_FILES, CC_SETTINGS, CC_XMB, CC_RESTART, CC_OFF, CC_COUNT };

static const char *cc_label(int item)
{
    switch (item) {
    case CC_MUSIC:    return L(music_state() == MUSIC_PLAYING ? S_PAUSE : S_PLAY);
    case CC_SLEEP:    return L(S_SLEEP);
    case CC_USB:      return L(S_USB_MODE);
    case CC_FILES:    return L(S_FILES);
    case CC_SETTINGS: return L(S_SETTINGS);
    case CC_XMB:      return L(S_TO_XMB);
    case CC_RESTART:  return L(S_RESTART);
    }
    return L(S_SHUTDOWN);
}

/* UI_SETTINGS / UI_ACTION (ui->cc_action) / UI_NONE */
static int update_cc(Ui *ui, const UiInput *in, float dt)
{
    ui->cc_anim = clampf(ui->cc_anim + (ui->cc_open ? 7 : -7) * dt, 0, 1);
    if (!ui->cc_open)
        return UI_NONE;
    int move = step_input(ui, in, dt, 0);
    if (move) {
        int next = clampf(ui->cc_sel + move, 0, CC_COUNT - 1);
        if (next != ui->cc_sel) {
            ui->cc_sel = next;
            ui->cc_confirm = 0;
            sound_play(SND_MOVE);
        }
    }
    u32 ok = ui->confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
    u32 back = (ui->confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE) | PSP_CTRL_HOME;
    if (in->pressed & back) {
        ui->cc_open = 0;
        sound_play(SND_BACK);
        return UI_NONE;
    }
    if (!(in->pressed & ok))
        return UI_NONE;
    static const int actions[CC_COUNT] = { 0, SA_SUSPEND, SA_USB, SA_FILES, 0, SA_XMB, SA_RESTART, SA_SHUTDOWN };
    switch (ui->cc_sel) {
    case CC_MUSIC:
        if (music_current() >= 0 && music_state() != MUSIC_STOPPED)
            music_toggle_pause();
        else if (music_count())
            music_play(ui->music_sel);
        else
            sound_play(SND_ERROR);
        return UI_NONE;
    case CC_SETTINGS:
        ui->cc_open = 0;
        return UI_SETTINGS;
    case CC_RESTART:
    case CC_OFF:
        if (!ui->cc_confirm) { /* som i inställningarna: ✕ två gånger */
            ui->cc_confirm = 1;
            sound_play(SND_SELECT);
            return UI_NONE;
        }
        break;
    }
    ui->cc_open = 0;
    ui->cc_confirm = 0;
    ui->cc_anim = 0; /* main.c bygger kanske upp allt på nytt (USB, filer) */
    ui->cc_action = actions[ui->cc_sel];
    sound_play(SND_SELECT);
    return UI_ACTION;
}

int ui_update(Ui *ui, const UiInput *in, float dt)
{
    ui->time += dt;
    ui->intro = clampf(ui->intro + dt * 2.2f, 0, 1);
    ui->tab_anim = clampf(ui->tab_anim + dt * 4.0f, 0, 1);
    if (ui->toast_time > 0)
        ui->toast_time -= dt;

    if (ui->launch >= 0) {
        preview_want(NULL, -1); /* spelets musik ska inte spela över startljudet */
        ui->launch += dt / LAUNCH_LEN;
        return ui->launch >= 1 ? UI_LAUNCH : UI_NONE;
    }
    /* alternativmenyn: ✕ väljer, ○ eller □ stänger – resten av gränssnittet får inga knappar */
    ui->menu_anim = clampf(ui->menu_anim + (ui->menu_open ? 7 : -7) * dt, 0, 1);
    static const UiInput none = { 0, 0, 128, 128 };
    /* HOME öppnar Control Center var man än är (utom under startanimationen) och stänger
     * andra paneler – före dem, så att ingen av dem sväljer knappen */
    if ((in->pressed & PSP_CTRL_HOME) && !ui->cc_open && ui->intro >= 1) {
        ui->cc_open = 1;
        ui->cc_confirm = 0;
        ui->menu_open = ui->cat_open = 0;
        sound_play(SND_OPEN);
        in = &none;
    }
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
    int cc_was_open = ui->cc_open;
    int cc = update_cc(ui, in, dt);
    if (cc != UI_NONE)
        return cc;
    if (cc_was_open)
        in = &none;
    int cat_was_open = ui->cat_open;
    update_categories(ui, in, dt);
    if (cat_was_open)
        in = &none;
    if ((in->pressed & PSP_CTRL_SELECT) && ui->tab != TAB_MUSIC && ui->intro >= 1) {
        open_categories(ui);
        in = &none;
    }
    if ((in->pressed & PSP_CTRL_TRIANGLE) && ui->intro >= 1)
        return UI_SETTINGS; /* ljudet spelas när inställningarna har lästs in (main.c) */
    if (in->pressed & PSP_CTRL_LTRIGGER)
        switch_tab(ui, ui->tab - 1);
    if (in->pressed & PSP_CTRL_RTRIGGER)
        switch_tab(ui, ui->tab + 1);

    int overlay = in == &none; /* en panel är öppen: knapprepetitionen tillhör den */
    if (ui->tab == TAB_MUSIC && !overlay)
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
    int move = ui->tab == TAB_MUSIC || overlay ? 0 : step_input(ui, in, dt, grid);
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

    /* spelets video och musik, som i XMB: efter en stund på samma kort i Spel eller Appar */
    int pv = -1;
    if (g_previews && (ui->tab == TAB_GAMES || ui->tab == TAB_APPS) && !overlay && ui->cat_anim <= 0 &&
        ui->cc_anim <= 0 && ui->intro >= 1 && ui->still >= PREVIEW_DELAY)
        pv = ui_selected(ui);
    preview_want(pv >= 0 ? &ui->list->items[pv] : NULL, pv);
    if (pv >= 0 && preview_frame(pv))
        ui->preview_anim = clampf(ui->preview_anim + dt * 4, 0, 1);
    else
        ui->preview_anim = 0;

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
    if (strcmp(s_wall_for, g_wallpaper)) { /* bytt i inställningarna – laddas sällan, från cachen */
        tex_free(s_wall);
        s_wall = art_load_wallpaper(g_wallpaper);
        snprintf(s_wall_for, sizeof(s_wall_for), "%s", g_wallpaper);
    }
    if (s_wall)
        gfx_tex(s_wall, 0, 0, SCREEN_W, SCREEN_H, RGBA(190, 190, 195, 255)); /* lite mörkare för texten */
    else
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
/* bolt_gap: extra plats mellan blixten och batteriet (för procent/tid kvar) */
static void draw_battery(float x, float y, int percent, int charging, float time, float a, float bolt_gap)
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
        float cx = x - 7 - bolt_gap, cy = y + 5;
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

/* Lokal tid. På en PSP-1000 med ARK-4 kan sceRtcGetCurrentClockLocalTime ge fel efter att
 * batteriet tagits ur, fast XMB visar rätt tid – räkna då själv med systemets tidszon
 * och sommartid, och i sista hand den råa klockan. */
static int read_clock(ScePspDateTime *now)
{
    if (sceRtcGetCurrentClockLocalTime(now) >= 0)
        return 1;
    int tz = 0, dst = 0;
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_TIMEZONE, &tz); /* minuter från UTC */
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS, &dst);
    if (sceRtcGetCurrentClock(now, tz + (dst ? 60 : 0)) >= 0)
        return 1;
    u64 tick;
    return sceRtcGetCurrentTick(&tick) >= 0 && sceRtcSetTick(now, &tick) >= 0;
}

static void draw_top_bar(Ui *ui)
{
    float a = ui->intro;

    ScePspDateTime now;
    char clock[8] = "";
    if (read_clock(&now))
        snprintf(clock, sizeof(clock), "%02d:%02d", now.hour, now.minute);
    text_draw(SCREEN_W - MARGIN, 25, 0.8f, 0, with_alpha(TEXT_MAIN, a), ALIGN_RIGHT, clock);
    float cw = text_width(0.8f, 0, clock);
    float bx = SCREEN_W - MARGIN - cw - 32;
    int battery = scePowerIsBatteryExist();
    int charging = testrun_charging() >= 0 ? testrun_charging() : battery && scePowerIsBatteryCharging() > 0;
    int level = testrun_battery() >= 0 ? testrun_battery() : battery ? scePowerGetBatteryLifePercent() : -1;
    /* procent eller tid kvar till vänster om batteriet (Inställningar → psp5 → Batteri).
     * Tid kvar finns bara när batteriet laddas ur – annars visas procent. */
    char info[12] = "";
    if (g_battery && level >= 0) {
        int mins = g_battery == 2 && !charging ? scePowerGetBatteryLifeTime() : -1;
        if (g_battery == 2) { /* felsökning: vad PSP:n faktiskt rapporterar (bara när något ändras) */
            static int last = -12345, last_chg = -1, last_online = -1;
            int raw = scePowerGetBatteryLifeTime(), online = scePowerIsPowerOnline();
            if (raw != last || charging != last_chg || online != last_online)
                preview_trace("batteri: tid kvar %d min (%08X), laddar %d, nätström %d, %d%%", raw, raw, charging,
                              online, level);
            last = raw;
            last_chg = charging;
            last_online = online;
        }
        if (mins > 0)
            snprintf(info, sizeof(info), "%d:%02d", mins / 60, mins % 60);
        else
            snprintf(info, sizeof(info), "%d%%", level);
    }
    float iw = info[0] ? text_width(0.6f, 0, info) + 5 : 0;
    if (info[0])
        text_draw(bx - 5, 24.5f, 0.6f, 0, with_alpha(TEXT_DIM, a), ALIGN_RIGHT, info);
    draw_battery(bx, 15.5f, level, charging, ui->time, a, iw);

    /* flikarna krymper om de inte får plats (t.ex. långa ryska ord) – mäts bara om när något ändras */
    static float size = 0.8f, gap = 20, sized_room = -1;
    static int sized_tab = -1, sized_cats = -1;
    static const char *sized_lang;
    /* fliken med ett kategorifilter heter t.ex. "Spel · GBA" */
    static char names[TAB_COUNT][64];
    int cats = ui->cat[TAB_GAMES] | ui->cat[TAB_APPS] << 8 | ui->cat[TAB_LIBRARY] << 16;
    float room = bx - 18 - iw - MARGIN; /* plats för laddningsblixten och procenten till vänster om batteriet */
    if (room != sized_room || ui->tab != sized_tab || L(TAB_NAMES[0]) != sized_lang || cats != sized_cats) {
        for (int t = 0; t < TAB_COUNT; t++) {
            if (t != TAB_MUSIC && ui->cat[t] > 0)
                snprintf(names[t], sizeof(names[t]), "%s · %s", L(TAB_NAMES[t]), cat_title(ui->cat[t]));
            else
                snprintf(names[t], sizeof(names[t]), "%s", L(TAB_NAMES[t]));
        }
        size = 0.8f;
        gap = 20;
        for (;;) {
            float w = 0;
            for (int t = 0; t < TAB_COUNT; t++)
                w += text_width(size, t == ui->tab, names[t]) + gap;
            if (w - gap <= room || size <= 0.56f)
                break;
            size -= 0.04f;
            gap = 14;
        }
        sized_room = room;
        sized_tab = ui->tab;
        sized_lang = L(TAB_NAMES[0]);
        sized_cats = cats;
    }
    float x = MARGIN;
    for (int t = 0; t < TAB_COUNT; t++) {
        int active = t == ui->tab;
        text_draw(x, 25, size, active, with_alpha(active ? TEXT_MAIN : RGBA(255, 255, 255, 120), a),
                  ALIGN_LEFT, names[t]);
        x += text_width(size, active, names[t]) + gap;
    }

    /* musik som spelas syns som en liten not med titeln i de andra flikarna */
    int cur = music_current();
    if (ui->tab != TAB_MUSIC && cur >= 0 && music_state() != MUSIC_STOPPED) {
        float right = bx - 16 - iw, left = x + 4;
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
    /* spelets video (ICON1.PMF) över ikonen på valt kort */
    Texture *video = focus > 0.5f && ui->preview_anim > 0 ? preview_frame(list_index) : NULL;
    if (video)
        gfx_tex(video, x, y, w, h, RGBA(255, 255, 255, (int)(255 * alpha * smooth(ui->preview_anim))));

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

    /* spelets logga (PIC0) till vänster under kortet, som titelloggan på PS5 – nederkanten
     * står fast ovanför typetiketten */
    if (ui->logo_cur && ui->bg_index == i) {
        Texture *lg = ui->logo_cur;
        float la = fade * smooth(ui->bg_fade) * (1 - smooth(ui->menu_anim));
        gfx_tex(lg, MARGIN, 188 - lg->h, lg->w, lg->h, RGBA(255, 255, 255, (int)(255 * la)));
    }

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

    /* rubrik: antal till vänster, sortering till höger */
    char head[96];
    snprintf(head, sizeof(head), "%s", ui->cat[TAB_LIBRARY] ? cat_title(ui->cat[TAB_LIBRARY]) : L(S_LIB_ALL));
    text_draw(MARGIN, 53, 0.66f, 0, with_alpha(TEXT_MAIN, ui_a), ALIGN_LEFT, head);
    char num[16];
    snprintf(num, sizeof(num), "%d", v->count);
    float hw = text_width(0.66f, 0, head);
    text_draw(MARGIN + hw + 8, 53, 0.6f, 0, with_alpha(TEXT_DIM, ui_a), ALIGN_LEFT, num);
    if (s_ncat) { /* SELECT Kategorier */
        float kx = MARGIN + hw + 8 + text_width(0.6f, 0, num) + 14;
        kx += key_cap(kx, 49, "SELECT", ui_a) + 5;
        text_draw(kx, 53, 0.56f, 0, with_alpha(TEXT_DIM, ui_a), ALIGN_LEFT, L(S_CATEGORIES));
    }

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

    /* --- spelas nu --- */
    float px = PANEL_X, pw = SCREEN_W - MARGIN - PANEL_X, py = MLIST_Y + slide, ph = MROWS * MROW_H - 2;
    gfx_round_rect(px, py, pw, ph, 10, RGBA(255, 255, 255, (int)(16 * a)));
    text_draw(px + 12, py + 17, 0.5f, 0, with_alpha(TEXT_DIM, a), ALIGN_LEFT, L(S_NOW_PLAYING));
    /* blanda (SELECT): två korsade pilar uppe till höger, ljusa när det är på */
    {
        float sx = px + pw - 22, sy = py + 13;
        u32 sc = with_alpha(LIGHT, (g_shuffle ? 0.95f : 0.3f) * a);
        gfx_line(sx - 6, sy + 4, sx + 5, sy - 4, sc);
        gfx_line(sx - 6, sy - 4, sx + 5, sy + 4, sc);
        float up[6] = { sx + 7, sy - 4, sx + 3, sy - 6.5f, sx + 3, sy - 1.5f };
        float dn[6] = { sx + 7, sy + 4, sx + 3, sy + 1.5f, sx + 3, sy + 6.5f };
        gfx_fan(sx + 4.3f, sy - 4, up, 3, sc);
        gfx_fan(sx + 4.3f, sy + 4, dn, 3, sc);
    }
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

/* ✕ Välj  ○ Tillbaka – längst ner i panelerna */
static void draw_select_back(Ui *ui, float x, float y, float a)
{
    gfx_button(x + 6.5f, y, 6.5f, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, with_alpha(LIGHT, 0.9f * a),
               with_alpha(DARK, a));
    x += 17;
    text_draw(x, y + 4.5f, 0.52f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, L(S_SELECT));
    x += text_width(0.52f, 0, L(S_SELECT)) + 12;
    gfx_button(x + 6.5f, y, 6.5f, ui->confirm_circle ? BTN_CROSS : BTN_CIRCLE, with_alpha(LIGHT, 0.9f * a),
               with_alpha(DARK, a));
    text_draw(x + 17, y + 4.5f, 0.52f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, L(S_BACK));
}

/* Kategoriväljaren: kategorimappens bg.png över hela skärmen, listan till vänster */
static void draw_categories(Ui *ui)
{
    float a = smooth(ui->cat_anim);
    if (a <= 0)
        return;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(8, 8, 12, (int)(200 * a)));
    if (ui->cat_bg && ui->cat_bg_for == ui->cat_sel)
        gfx_tex(ui->cat_bg, 0, 0, SCREEN_W, SCREEN_H, RGBA(205, 205, 210, (int)(255 * a * smooth(ui->cat_bg_fade))));
    gfx_gradient_h(0, 0, 300, SCREEN_H, RGBA(0, 0, 0, (int)(170 * a)), RGBA(0, 0, 0, 0));

    float slide = (1 - a) * -16;
    text_draw(MARGIN + slide, 34, 0.95f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, L(S_CATEGORIES));
    float lw = 236;
    gfx_clip(0, 50, SCREEN_W, CAT_ROWS * CAT_ROW_H + 6);
    for (int r = 0; r < CAT_ROWS && ui->cat_scroll + r < s_npick; r++) {
        int i = (int)ui->cat_scroll + r, c = s_pick[i];
        float y = 54 + r * CAT_ROW_H;
        int selected = i == ui->cat_sel;
        if (selected)
            gfx_round_rect(MARGIN - 6 + slide, y, lw, CAT_ROW_H - 3, 7, with_alpha(LIGHT, 0.95f * a));
        u32 main = with_alpha(selected ? DARK : TEXT_MAIN, a);
        char name[48], num[12];
        text_ellipsis(name, sizeof(name), cat_title(c), 0.62f, lw - 60);
        snprintf(num, sizeof(num), "%d", s_cat_n[ui->tab][c]);
        text_draw(MARGIN + 4 + slide, y + 16.5f, 0.62f, 0, main, ALIGN_LEFT, name);
        text_draw(MARGIN - 16 + lw + slide, y + 16.5f, 0.52f, 0,
                  selected ? RGBA(70, 72, 80, (int)(255 * a)) : with_alpha(TEXT_DIM, a), ALIGN_RIGHT, num);
    }
    gfx_clip_reset();
    draw_select_back(ui, MARGIN, 246, a);
}

/* Control Center: ikonerna ritas med enkla former i färgen c, med "hål" i bakgrundsfärgen bg */
static void draw_cc_glyph(int item, float cx, float cy, u32 c, u32 bg)
{
    switch (item) {
    case CC_MUSIC:
        draw_play_glyph(cx, cy, 8, music_state() == MUSIC_PLAYING, c);
        break;
    case CC_SLEEP: /* måne */
        gfx_circle(cx, cy, 8, c);
        gfx_circle(cx + 4, cy - 3, 6.8f, bg);
        break;
    case CC_USB:
        text_draw(cx, cy + 4, 0.45f, 1, c, ALIGN_CENTER, "USB");
        break;
    case CC_FILES: /* mapp */
        gfx_round_rect(cx - 9, cy - 7.5f, 8, 5, 1.5f, c);
        gfx_round_rect(cx - 9, cy - 5, 18, 12, 2, c);
        break;
    case CC_SETTINGS: /* kugghjul */
        for (int k = 0; k < 8; k++)
            gfx_circle(cx + cosf(k * 0.785f) * 7.5f, cy + sinf(k * 0.785f) * 7.5f, 2.4f, c);
        gfx_circle(cx, cy, 7, c);
        gfx_circle(cx, cy, 3, bg);
        break;
    case CC_XMB:
        text_draw(cx, cy + 4, 0.45f, 1, c, ALIGN_CENTER, "XMB");
        break;
    case CC_RESTART: { /* ring med pil */
        gfx_circle(cx, cy, 8.5f, c);
        gfx_circle(cx, cy, 6, bg);
        gfx_rect(cx, cy - 10, 10, 8, bg);
        float tri[6] = { cx - 1, cy - 12.5f, cx + 4.5f, cy - 7.25f, cx - 1, cy - 2 };
        gfx_fan(cx + 1, cy - 7.25f, tri, 3, c);
        break;
    }
    default: /* av/på-symbolen */
        gfx_circle(cx, cy + 1, 8.5f, c);
        gfx_circle(cx, cy + 1, 6, bg);
        gfx_rect(cx - 3.5f, cy - 11, 7, 9, bg);
        gfx_round_rect(cx - 1.4f, cy - 10, 2.8f, 10, 1.4f, c);
        break;
    }
}

/* Control Center: en panel nerifrån med en rad runda knappar, som PS5:s */
static void draw_cc(Ui *ui)
{
    float a = smooth(ui->cc_anim);
    if (a <= 0)
        return;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(120 * a)));
    float ph = 96, py = SCREEN_H - ph * a;
    gfx_glow(0, py, SCREEN_W, ph + 20, 12, RGBA(0, 0, 0, (int)(150 * a)));
    gfx_round_rect(0, py, SCREEN_W, ph + 20, 14, RGBA(30, 31, 37, (int)(250 * a)));

    /* valt alternativ ovanför knapparna, och ○ Tillbaka till höger */
    const char *label = ui->cc_confirm ? L(S_CONFIRM) : cc_label(ui->cc_sel);
    text_draw(MARGIN, py + 22, 0.66f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, label);
    float bw = text_width(0.52f, 0, L(S_BACK));
    gfx_button(SCREEN_W - MARGIN - bw - 10.5f, py + 18, 6.5f, ui->confirm_circle ? BTN_CROSS : BTN_CIRCLE,
               with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    text_draw(SCREEN_W - MARGIN, py + 22.5f, 0.52f, 0, with_alpha(TEXT_MAIN, a), ALIGN_RIGHT, L(S_BACK));

    const float r = 19, gap = 13;
    float x0 = (SCREEN_W - CC_COUNT * 2 * r - (CC_COUNT - 1) * gap) / 2 + r, cy = py + 58;
    u32 tile = RGBA(58, 59, 67, 255);
    for (int k = 0; k < CC_COUNT; k++) {
        float cx = x0 + k * (2 * r + gap);
        int sel = k == ui->cc_sel;
        u32 bg = sel ? LIGHT : tile;
        gfx_circle(cx, cy, r, with_alpha(bg, a));
        if (sel)
            gfx_ring(cx - r - 3.5f, cy - r - 3.5f, 2 * r + 7, 2 * r + 7, r + 3.5f, with_alpha(LIGHT, 0.9f * a));
        draw_cc_glyph(k, cx, cy, with_alpha(sel ? DARK : LIGHT, a), with_alpha(bg, a));
    }
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
    /* Bibliotek och Musik: mörkare yta än i raden – valt spels bild skymtar bara svagt.
     * Före toppraden, så att den är lika ljus i alla flikar. */
    if (ui->tab == TAB_LIBRARY && view(ui)->count)
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(8, 8, 12, 165));
    else if (ui->tab == TAB_MUSIC)
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(8, 8, 12, 150));
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
    draw_categories(ui);
    draw_cc(ui);
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
