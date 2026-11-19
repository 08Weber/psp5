/* psp5 – made by 08Weber */
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
#include "datetime.h"
#include "settings.h" /* SA_* för Control Center */
#include "boot.h"      /* psp5-loggan */
#include "icons.h"     /* sök- och inställningsikonerna */
#include "iso.h"       /* UMD Video-ISO:er i Videor */
#include "umd.h"

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
#define LIB_MARGIN 16.0f
#define GRID_COLS  5
#define GRID_GAP   7.0f
#define GRID_Y     72.0f   /* första radens överkant */
#define CELL_W     ((SCREEN_W - 2 * LIB_MARGIN - (GRID_COLS - 1) * GRID_GAP) / GRID_COLS)
#define CELL_H     (CELL_W * 80.0f / 144.0f) /* ikonernas format */
#define ROW_STEP   (CELL_H + 10.0f)
#define GRID_ROWS  3       /* hela rader som syns */
#define FOOTER_Y   242.0f  /* dockad fotlist längst ner (delas av alla skärmar) */
#define GRID_BOTTOM FOOTER_Y /* rutnätet klipps här, ovanför fotlisten */

/* musik: låtlista till vänster, "spelas nu" till höger */
#define MLIST_Y       50.0f
#define MROW_H        26.0f
#define MROWS         7
#define PANEL_X       306.0f
#define MUSIC_LIST_X  12.0f
#define MUSIC_LIST_Y  36.0f
#define MUSIC_ROW_H   28.0f
#define MUSIC_ROWS    7
#define MUSIC_LIST_W  232.0f
#define MUSIC_PANEL_X 256.0f
#define MUSIC_PANEL_H 204.0f

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

/* --- senast spelat, egen ordning och fästa spel ---
 * <data>/played.txt (senast spelat först), order.txt (egen ordning, Flytta) och pins.txt: en nyckel
 * (entry_cover_key) per rad.
 * I minnet ligger de i s_meta, parallellt med spellistan. */

typedef struct {
    u32 played; /* högre = spelat senare, 0 = aldrig */
    int rank;   /* plats i egen ordning (1 = först), 0 = ingen */
    u8 pinned;
} Meta;

static Meta *s_meta;
static int s_meta_n;
static u32 s_played_max;
#define MAX_PLAYED 200 /* så många rader sparas i played.txt */

static Meta *meta(int i)
{
    static Meta none;
    return s_meta && i >= 0 && i < s_meta_n ? &s_meta[i] : &none;
}

static u32 key_hash(const char *s)
{
    u32 h = 2166136261u;
    while (*s)
        h = (h ^ (u8)*s++) * 16777619u;
    return h;
}

typedef struct { u32 h; int i; } KeyRef;
static int cmp_ref(const void *a, const void *b)
{
    u32 x = ((const KeyRef *)a)->h, y = ((const KeyRef *)b)->h;
    return x < y ? -1 : x > y;
}

/* Läser <data>/<name> och anropar fn(listindex, rad, antal rader) för varje post med radens
 * nyckel. Nycklarna slås upp via en sorterad hashtabell – listan kan ha tusen spel. */
static void keyfile_apply(const EntryList *l, const char *name, void (*fn)(int i, int line, int lines))
{
    char path[64], key[80];
    snprintf(path, sizeof(path), "%s/%s", g_data_dir, name);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return;
    int size = sceIoLseek32(fd, 0, PSP_SEEK_END);
    sceIoLseek32(fd, 0, PSP_SEEK_SET);
    char *buf = size > 0 && size < 256 * 1024 ? malloc(size + 1) : NULL;
    int n = buf ? sceIoRead(fd, buf, size) : -1;
    sceIoClose(fd);
    KeyRef *refs = n > 0 && l->count ? malloc(l->count * sizeof(KeyRef)) : NULL;
    if (!refs) {
        free(buf);
        return;
    }
    buf[n] = 0;
    for (int i = 0; i < l->count; i++) {
        entry_cover_key(&l->items[i], key, sizeof(key));
        refs[i] = (KeyRef){ key_hash(key), i };
    }
    qsort(refs, l->count, sizeof(KeyRef), cmp_ref);
    int lines = 0;
    for (char *p = buf; *p; p++)
        lines += *p == '\n';
    int line = 0;
    for (char *p = buf; *p; line++) {
        char *end = strchr(p, '\n');
        if (end)
            *end = 0;
        int len = strlen(p);
        if (len && p[len - 1] == '\r')
            p[len - 1] = 0;
        u32 h = key_hash(p);
        int lo = 0, hi = l->count;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (refs[mid].h < h)
                lo = mid + 1;
            else
                hi = mid;
        }
        for (; *p && lo < l->count && refs[lo].h == h; lo++) {
            entry_cover_key(&l->items[refs[lo].i], key, sizeof(key));
            if (!strcmp(key, p))
                fn(refs[lo].i, line, lines);
        }
        if (!end)
            break;
        p = end + 1;
    }
    free(refs);
    free(buf);
}

/* Samma nyckel kan finnas flera gånger (ISO och CSO av samma spel): första raden gäller */
static void set_played(int i, int line, int lines)
{
    if ((u32)(lines - line) > s_meta[i].played)
        s_meta[i].played = lines - line;
    if (s_meta[i].played > s_played_max)
        s_played_max = s_meta[i].played;
}
static void set_rank(int i, int line, int lines)
{
    if (!s_meta[i].rank)
        s_meta[i].rank = line + 1;
}
static void set_pinned(int i, int line, int lines) { s_meta[i].pinned = 1; }

static void meta_load(const EntryList *l)
{
    free(s_meta);
    s_meta = calloc(l->count ? l->count : 1, sizeof(Meta));
    s_meta_n = s_meta ? l->count : 0;
    s_played_max = 0;
    if (!s_meta)
        return;
    keyfile_apply(l, "played.txt", set_played);
    keyfile_apply(l, "order.txt", set_rank);
    keyfile_apply(l, "pins.txt", set_pinned);
}

/* Skriver nycklarna för listindexen idx[0..n) till <data>/<name>, en per rad */
static void keyfile_write(const EntryList *l, const char *name, const int *idx, int n)
{
    /* allt i en buffert och en enda skrivning – tusen små skrivningar gav ett märkbart häng */
    char path[64], *buf = malloc(n * 80 + 1);
    if (!buf)
        return;
    int len = 0;
    for (int k = 0; k < n; k++) {
        entry_cover_key(&l->items[idx[k]], buf + len, 79);
        len += strlen(buf + len);
        buf[len++] = '\n';
    }
    snprintf(path, sizeof(path), "%s/%s", g_data_dir, name);
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, buf, len);
        sceIoClose(fd);
    }
    free(buf);
}

static int cmp_played_desc(const void *a, const void *b)
{
    u32 x = s_meta[*(const int *)a].played, y = s_meta[*(const int *)b].played;
    return x > y ? -1 : x < y;
}
static int cmp_rank(const void *a, const void *b)
{
    int x = s_meta[*(const int *)a].rank, y = s_meta[*(const int *)b].rank;
    return x != y ? x - y : *(const int *)a - *(const int *)b;
}

/* Sparar played.txt, order.txt eller pins.txt ur s_meta. UMD-skivan sparas aldrig. */
enum { META_PLAYED, META_ORDER, META_PINS };
static void meta_save(const EntryList *l, int which)
{
    int *idx = s_meta ? malloc((l->count ? l->count : 1) * sizeof(int)) : NULL;
    if (!idx)
        return;
    int n = 0;
    for (int i = 0; i < l->count; i++) {
        const Meta *m = &s_meta[i];
        if (l->items[i].type != ENTRY_UMD &&
            (which == META_PLAYED ? m->played > 0 : which == META_ORDER ? m->rank > 0 : m->pinned))
            idx[n++] = i;
    }
    if (which == META_PLAYED) {
        qsort(idx, n, sizeof(int), cmp_played_desc);
        if (n > MAX_PLAYED)
            n = MAX_PLAYED;
    } else if (which == META_ORDER) {
        qsort(idx, n, sizeof(int), cmp_rank);
    }
    keyfile_write(l, which == META_PLAYED ? "played.txt" : which == META_ORDER ? "order.txt" : "pins.txt", idx, n);
    free(idx);
}

/* --- kategorier --- */

#define MAX_CATS 64
static char s_cat_name[MAX_CATS][40]; /* mappnamnet, t.ex. "g_GBA" */
static char s_cat_dir[MAX_CATS][96];  /* mappen (för bg.png), t.ex. "ms0:/PSP/GAME/g_GBA" */
static int s_ncat;
static int s_cat_n[TAB_COUNT][MAX_CATS + 1]; /* antal kort per flik och kategori, [t][0] = alla (även de i kategorier) */
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
    if (ui->tab == TAB_SEARCH)
        return ui->search_launch_idx;
    if (ui->tab >= TAB_MUSIC)
        return -1;
    const LoaderView *v = view(ui);
    int sel = ui->sel[ui->tab];
    return v->count && sel >= 0 && sel < v->count ? v->idx[sel] : -1;
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

/* Spel-raden: UMD-skivan, fästa spel, sedan senast spelat. Aldrig spelade: nyaste mappen först
 * (som XMB). Lika: listans ordning (A–Ö). Flyttade kort (egen ordning) går före: bland fästa
 * alltid, bland ofästa bara när raden visar alla spel – annars styr senast spelat. */
#define RECENT_GAMES 8 /* ofästa spel i raden – resten finns i Spelbiblioteket */
static int cmp_row(const void *a, const void *b)
{
    int ia = *(const int *)a, ib = *(const int *)b;
    const Entry *x = &s_sort_list->items[ia], *y = &s_sort_list->items[ib];
    const Meta *mx = meta(ia), *my = meta(ib);
    if ((x->type == ENTRY_UMD) != (y->type == ENTRY_UMD))
        return x->type == ENTRY_UMD ? -1 : 1;
    if (mx->pinned != my->pinned)
        return mx->pinned ? -1 : 1;
    if ((mx->pinned || g_all_games) && mx->rank != my->rank) /* utan plats (nya spel) sist */
        return (u32)(mx->rank - 1) < (u32)(my->rank - 1) ? -1 : 1;
    if (mx->played != my->played)
        return mx->played > my->played ? -1 : 1;
    if (x->sort_time != y->sort_time)
        return x->sort_time > y->sort_time ? -1 : 1;
    return ia - ib;
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

static int is_app_entry(const Entry *e)
{
    if (e->type == ENTRY_UMD)
        return 0;
    if (!strncasecmp(e->category, "s_", 2))
        return 1;
    if (!strncasecmp(e->category, "g_", 2))
        return 0;
    return e->type == ENTRY_HOMEBREW;
}

static void finalize_row_view(Ui *ui, int tab)
{
    LoaderView *v = &ui->views[tab];
    if (!v->idx)
        return;
    qsort(v->idx, v->count, sizeof(int), cmp_row);
    int keep = 0; /* skivan och fästa kort står alltid kvar */
    while (keep < v->count && (ui->list->items[v->idx[keep]].type == ENTRY_UMD || meta(v->idx[keep])->pinned))
        keep++;
    /* i en vald kategori syns alla dess kort – gränsen gäller bara "Alla" (issue #39) */
    if (!g_all_games && !ui->cat[tab] && v->count > keep + RECENT_GAMES)
        v->count = keep + RECENT_GAMES;
}

static void build_views(Ui *ui)
{
    collect_cats(ui->list);
    memset(s_cat_n, 0, sizeof(s_cat_n));
    for (int i = 0; i < ui->list->count; i++) {
        if (is_dupe(ui->list, i))
            continue;
        const Entry *e = &ui->list->items[i];
        int c = cat_of(e);
        int primary = is_app_entry(e) ? TAB_APPS : TAB_GAMES;
        s_cat_n[primary][c]++;
        s_cat_n[TAB_LIBRARY][c]++;
        if (c) { /* "Alla" räknar även korten i kategorierna (issue #39) */
            s_cat_n[primary][0]++;
            s_cat_n[TAB_LIBRARY][0]++;
        }
    }
    for (int t = 0; t < TAB_COUNT; t++) {
        ui->views[t].idx = malloc((ui->list->count ? ui->list->count : 1) * sizeof(int));
        ui->views[t].count = 0;
        if (ui->cat[t] < 0 || ui->cat[t] > s_ncat || !s_cat_n[t][ui->cat[t]])
            ui->cat[t] = 0; /* kategorin är tom eller borta */
    }
    for (int i = 0; i < ui->list->count; i++) {
        if (is_dupe(ui->list, i))
            continue;
        const Entry *e = &ui->list->items[i];
        int c = cat_of(e);
        int primary = is_app_entry(e) ? TAB_APPS : TAB_GAMES;
        LoaderView *pv = &ui->views[primary];
        /* UMD-skivan syns i alla spelkategorier; appar filtreras till TAB_APPS och alla ligger i Bibliotek */
        if (pv->idx && (!ui->cat[primary] || c == ui->cat[primary] || e->type == ENTRY_UMD))
            pv->idx[pv->count++] = i;
        LoaderView *lib = &ui->views[TAB_LIBRARY];
        if (lib->idx && (!ui->cat[TAB_LIBRARY] || c == ui->cat[TAB_LIBRARY] || e->type == ENTRY_UMD))
            lib->idx[lib->count++] = i;
    }
    s_sort_list = ui->list;
    finalize_row_view(ui, TAB_GAMES);
    finalize_row_view(ui, TAB_APPS);
    sort_library(ui);
}

/* Bibliotekets sortering (□-menyn): byt men behåll markeringen på samma spel */
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

static float row_entry_offset(int tab, int intro)
{
    if (tab == TAB_GAMES || tab == TAB_APPS)
        return intro ? -0.75f : -0.45f;
    return intro ? 1.5f : 0.7f;
}

static void switch_tab(Ui *ui, int tab)
{
    if (tab == ui->tab || tab < 0 || tab >= TAB_COUNT)
        return;
    if (tab == TAB_SEARCH) /* ○ i sökningen går tillbaka hit */
        ui->search_from = ui->tab;
    ui->tab = tab;
    ui->tab_anim = 0;
    sound_play(SND_SWIPE);
    ui->pos = ui->sel[tab] + row_entry_offset(tab, 0); /* raden glider in mjukt */
    ui->still = 0;
    ui->label_for = -999;
    ui->seek_dir = ui->seeking = 0; /* en hållen pil gäller bara i musikfliken */
    ui->pic_viewing = 0;
    ui->vid_viewing = 0;
    ui->search_launch_idx = -1;
    if (tab < TAB_MUSIC) /* musik- och bildflikarna har inga spelkort – låt ikonerna ligga kvar */
        loader_set_view(view(ui));
}

void ui_set_tab(Ui *ui, int tab)
{
    switch_tab(ui, tab);
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

static int label_key(const Ui *ui)
{
    if ((ui->tab == TAB_GAMES || ui->tab == TAB_APPS) && view(ui)->count && ui->sel[ui->tab] == view(ui)->count)
        return -3; /* Spelbibliotekskortet längst till höger */
    if (ui->tab == TAB_MEDIA)
        return -10 - ui->sel[TAB_MEDIA];
    return ui_selected(ui);
}

static void fit_label(Ui *ui)
{
    int key = label_key(ui);
    const float max_w = SCREEN_W - SMALL_X - MARGIN;
    ui->label_for = key;
    ui->label[0] = 0;
    if (key == -3) {
        ui->label_size = 0.78f;
        snprintf(ui->label, sizeof(ui->label), "%s", L(S_TAB_LIBRARY));
        return;
    }
    if (ui->tab == TAB_MEDIA) {
        static const StrId media_names[3] = { S_TAB_MUSIC, S_TAB_PICTURES, S_TAB_VIDEO };
        int m = clampf(ui->sel[TAB_MEDIA], 0, 2);
        ui->label_size = 0.78f;
        snprintf(ui->label, sizeof(ui->label), "%s", L(media_names[m]));
        return;
    }
    if (key < 0)
        return;
    const char *title = ui->list->items[key].title;
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
    ui->label_for = -999;
    ui->lib_label_for = -2;
    ui->cover_for = -2;
    ui->pic_tex_for = -1;
    ui->vid_thumb_for = -1;
    ui->search_launch_idx = -1;
    ui->tab_anim = 1;
    ui->accent[0] = ui->accent[1] = ui->accent[2] = 120;
    ui->cat_bg_for = -1;
    collect_cats(list); /* last_load letar kategorierna med namn */
    meta_load(list);    /* fästa spel, senast spelat och egen ordning – före sorteringen */
    char keys[TAB_MUSIC][80] = { "" };
    int had_last = last_load(ui, keys);
    build_views(ui);
    if (!had_last || (ui->tab != TAB_GAMES && ui->tab != TAB_APPS && ui->tab != TAB_LIBRARY && ui->tab != TAB_MUSIC))
        ui->tab = (had_last && (ui->tab == TAB_MEDIA || ui->tab == TAB_PICTURES || ui->tab == TAB_VIDEO))
                      ? TAB_MUSIC : TAB_GAMES;
    char key[80];
    for (int t = 0; t < TAB_MUSIC; t++)
        for (int p = 0; keys[t][0] && p < ui->views[t].count; p++) {
            entry_cover_key(&list->items[ui->views[t].idx[p]], key, sizeof(key));
            if (!strcmp(key, keys[t])) {
                ui->sel[t] = p;
                break;
            }
        }
    ui->pos = (ui->tab >= TAB_MUSIC ? 0 : ui->sel[ui->tab]) + row_entry_offset(ui->tab, 1); /* korten glider in vid start */
    ui->grid_target = ui->sel[TAB_LIBRARY] / GRID_COLS;
    ui->grid_scroll = ui->grid_target;
    loader_set_view(&ui->views[ui->tab >= TAB_MUSIC ? TAB_GAMES : ui->tab]);
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
    if ((removed_front || added_front) && s_meta) { /* s_meta följer listans index */
        Meta *m = calloc(ui->list->count ? ui->list->count : 1, sizeof(Meta));
        for (int i = added_front; m && i < ui->list->count; i++) {
            int old = i - added_front + removed_front;
            if (old >= 0 && old < s_meta_n)
                m[i] = s_meta[old];
        }
        if (m) {
            free(s_meta);
            s_meta = m;
            s_meta_n = ui->list->count;
        }
    }
    for (int t = 0; t < TAB_COUNT; t++) {
        LoaderView *v = &ui->views[t];
        chosen[t] = -1;
        if (t < TAB_MUSIC && v->count && v->idx && ui->sel[t] < v->count)
            chosen[t] = remap_index(v->idx[ui->sel[t]], removed_front, added_front);
        /* står man först i fliken blir en nyinsatt skiva markerad */
        if (added_front && !ui->moving && (t == TAB_GAMES || t == TAB_LIBRARY) && ui->sel[t] == 0)
            chosen[t] = 0;
        free(v->idx);
        v->idx = NULL;
        v->count = 0;
    }
    build_views(ui);
    for (int t = 0; t < TAB_COUNT; t++) {
        if (t >= TAB_MUSIC)
            continue;
        LoaderView *v = &ui->views[t];
        ui->sel[t] = 0;
        for (int p = 0; p < v->count; p++)
            if (v->idx[p] == chosen[t])
                ui->sel[t] = p;
    }
    if (ui->tab < TAB_MUSIC)
        ui->pos += ui->sel[ui->tab] - old_sel; /* raden står still om samma kort är markerat */
    ui->still = 0;
    ui->bg_index = remap_index(ui->bg_index, removed_front, added_front);
    ui->label_for = -2;
    ui->lib_label_for = -2;
    ui->menu_open = 0; /* kortet menyn gällde kan ha flyttats */
    if (ui->tab < TAB_MUSIC)
        loader_set_view(view(ui));
}

/* □-menyn för valt kort: Fäst, Flytta (i Spel-raden: fästa kort, eller alla när raden visar
 * alla spel – issue #31) och i biblioteket sorteringen */
enum { MI_PIN, MI_MOVE, MI_SORT, MI_INFO };

static int menu_items(const Ui *ui, int out[4])
{
    int n = 0;
    int sel = ui_selected(ui);
    if (sel >= 0 && ui->list->items[sel].type == ENTRY_UMD) { /* the disc: only Information (it cannot be pinned or moved) */
        out[n++] = MI_INFO;
        return n;
    }
    out[n++] = MI_PIN;
    out[n++] = MI_INFO;
    if ((ui->tab == TAB_GAMES || ui->tab == TAB_APPS) && s_meta && (g_all_games || meta(ui_selected(ui))->pinned))
        out[n++] = MI_MOVE;
    if (ui->tab == TAB_LIBRARY)
        out[n++] = MI_SORT;
    return n;
}

static int menu_count(const Ui *ui)
{
    int items[4];
    return menu_items(ui, items);
}

static const char *menu_label(const Ui *ui, int k, char *buf, int size)
{
    int items[4];
    if (k >= menu_items(ui, items)) /* menyn tonas ut och har just tappat en rad */
        return "";
    switch (items[k]) {
    case MI_PIN:  return L(meta(ui_selected(ui))->pinned ? S_UNPIN : S_PIN);
    case MI_MOVE: return L(S_F_MOVE);
    case MI_INFO: return L(S_INFO);
    }
    snprintf(buf, size, "%s: %s", L(S_SORT), L(ui->lib_sort ? S_SORT_RECENT : S_SORT_NAME));
    return buf;
}

/* Bygger om flikarna; raden glider från där den stod till kortets nya plats */
static void resort(Ui *ui)
{
    float pos = ui->pos;
    ui_relist(ui, 0, 0);
    ui->pos = pos;
}

/* Flytta: ger alla kort en egen, unik plats i den ordning de syns nu, så att inget hoppar */
static void start_arrange(Ui *ui)
{
    int *idx = s_meta ? malloc((s_meta_n ? s_meta_n : 1) * sizeof(int)) : NULL;
    if (!idx) {
        sound_play(SND_ERROR);
        return;
    }
    for (int i = 0; i < s_meta_n; i++)
        idx[i] = i;
    s_sort_list = ui->list;
    qsort(idx, s_meta_n, sizeof(int), cmp_row);
    for (int k = 0; k < s_meta_n; k++) /* ofästa bara när raden visar alla – annars styr senast spelat */
        if (s_meta[idx[k]].pinned || g_all_games)
            s_meta[idx[k]].rank = k + 1;
    free(idx);
    resort(ui);
    ui->moving = 1;
}

static void stop_arrange(Ui *ui)
{
    if (!ui->moving)
        return;
    ui->moving = 0;
    meta_save(ui->list, META_ORDER);
}

/* ← →: byter plats med grannen i raden (bara bland fästa eller bland ofästa) */
static void arrange_step(Ui *ui, int dir)
{
    LoaderView *v = &ui->views[ui->tab];
    int *sel = &ui->sel[ui->tab], nb = *sel + dir;
    if (nb < 0 || nb >= v->count)
        return;
    int a = v->idx[*sel], b = v->idx[nb];
    if (meta(a)->pinned != meta(b)->pinned || ui->list->items[a].type == ENTRY_UMD ||
        ui->list->items[b].type == ENTRY_UMD) {
        if (ui->repeat_held == 0) /* bara första trycket, inte när knappen hålls */
            sound_play(SND_ERROR);
        return;
    }
    int r = meta(a)->rank;
    meta(a)->rank = meta(b)->rank;
    meta(b)->rank = r;
    v->idx[*sel] = b;
    v->idx[nb] = a;
    *sel = nb;
    ui->still = 0;
    sound_play(SND_TILE);
}

static void menu_choose(Ui *ui)
{
    int i = ui_selected(ui), items[4];
    menu_items(ui, items);
    switch (items[ui->menu_sel]) {
    case MI_PIN:
        if (!s_meta) { /* inget minne för listan – då går det inte att fästa */
            sound_play(SND_ERROR);
            break;
        }
        meta(i)->pinned ^= 1;
        meta_save(ui->list, META_PINS);
        ui->menu_open = 0;
        resort(ui);
        sound_play(meta(i)->pinned ? SND_TOGGLE : SND_TOGGLE_OFF);
        break;
    case MI_INFO:
        ui->menu_open = 0;
        ui->want_info = 1; /* ui_update hands UI_INFO to main.c */
        break;
    case MI_MOVE:
        ui->menu_open = 0;
        start_arrange(ui);
        sound_play(SND_SELECT);
        break;
    case MI_SORT: /* menyn står kvar – nästa tryck nästa sortering */
        toggle_sort(ui);
        break;
    }
}

static void media_free(void);

void ui_free(Ui *ui)
{
    if (ui->list) {
        stop_arrange(ui); /* en påbörjad flytt sparas */
        last_save(ui);
    }
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
    tex_free(ui->pic_tex);
    ui->pic_tex = NULL;
    ui->pic_tex_for = -1;
    ui->pic_viewing = 0;
    tex_free(ui->vid_thumb);
    ui->vid_thumb = NULL;
    ui->vid_thumb_for = -1;
    media_free();
    tex_free(s_wall);
    s_wall = NULL;
    strcpy(s_wall_for, "\1");
    for (int t = 0; t < TAB_COUNT; t++) {
        free(ui->views[t].idx);
        ui->views[t].idx = NULL;
        ui->views[t].count = 0;
    }
    ui->list = NULL; /* last.txt sparas bara en gång */
    free(s_meta);
    s_meta = NULL;
    s_meta_n = 0;
}

void ui_replay_intro(Ui *ui)
{
    ui->intro = 0;
    if (ui->tab < TAB_MUSIC)
        ui->pos = ui->sel[ui->tab] + row_entry_offset(ui->tab, 1);
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

/* Nästa plats bland n poster: ett nytt tryck i kanten går runt till andra änden, en hållen
 * knapp stannar i kanten – så att man inte råkar rulla förbi sista spelet (issue #32) */
static int step_wrap(const Ui *ui, int cur, int delta, int n)
{
    int next = cur + delta;
    if (next >= 0 && next < n)
        return next;
    if (ui->repeat_held > 0 || n <= 0)
        return cur;
    return (next % n + n) % n;
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

/* Album: låtlistan är sorterad efter artist och album, så ett album är en följd låtar med
 * samma albumnamn. shortcut: samlingsskivor med flera artister blir flera album – slå ihop
 * dem om någon saknar det. s_alb_first[k] = första låten i album k, s_alb_first[s_nalb] = antalet. */
#define MAX_ALBUMS 300
static int s_alb_first[MAX_ALBUMS + 1], s_nalb, s_alb_gen = -1, s_alb_count = -1;

static void ensure_albums(void)
{
    int n = music_count();
    if (s_alb_gen == music_generation() && s_alb_count == n)
        return;
    s_alb_gen = music_generation();
    s_alb_count = n;
    s_nalb = 0;
    const Track *prev = NULL;
    for (int i = 0; i < n && s_nalb < MAX_ALBUMS; i++) {
        const Track *t = music_track(i);
        if (t && (!prev || strcmp(t->album, prev->album)))
            s_alb_first[s_nalb++] = i;
        prev = t;
    }
    s_alb_first[s_nalb] = n;
}

/* Låtarna som visas: [lo, hi) – alla, eller det valda albumet (music_album 1..s_nalb) */
static void music_range(const Ui *ui, int *lo, int *hi)
{
    ensure_albums();
    int k = ui->music_album;
    *lo = k >= 1 && k <= s_nalb ? s_alb_first[k - 1] : 0;
    *hi = k >= 1 && k <= s_nalb ? s_alb_first[k] : music_count();
}

/* Musikfliken: ↑↓ väljer, ✕ spelar eller pausar, □ blanda, ← → föregående/nästa låt,
 * △ albumlistan (✕ väljer album, ○ tillbaka) */
static void update_music(Ui *ui, const UiInput *in, float dt)
{
    int n = music_count(), lo, hi;
    music_range(ui, &lo, &hi);
    if (ui->music_album > s_nalb) /* listan har lästs om */
        ui->music_album = 0;
    if (ui->album_sel > s_nalb)
        ui->album_sel = 0;
    if (ui->music_sel < lo || ui->music_sel >= hi)
        ui->music_sel = lo;
    UiInput vert = *in; /* ← → ska inte upprepas när de hålls in */
    vert.held &= ~(PSP_CTRL_LEFT | PSP_CTRL_RIGHT);
    vert.lx = 128;
    int move = step_input(ui, &vert, dt, 1) / 2;
    if (move && ui->music_view) {
        int next = step_wrap(ui, ui->album_sel, move, s_nalb + 1);
        if (next != ui->album_sel) {
            ui->album_sel = next;
            sound_play(SND_MOVE);
        }
    } else if (move && hi > lo) {
        int next = lo + step_wrap(ui, ui->music_sel - lo, move, hi - lo);
        if (next != ui->music_sel) {
            ui->music_sel = next;
            sound_play(SND_MOVE);
        }
    }
    if (ui->intro < 1)
        return;
    u32 back_btn = ui->confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;
    if (in->pressed & back_btn) {
        if (ui->music_view) { /* albumlistan → låtlistan */
            ui->music_view = 0;
        } else if (ui->music_album) { /* ett album → albumlistan */
            ui->music_view = 1;
            ui->album_sel = ui->music_album;
        } else {
            switch_tab(ui, TAB_GAMES);
        }
        sound_play(SND_BACK);
        return;
    }
    if (!n)
        return;
    if (in->pressed & PSP_CTRL_TRIANGLE) {
        ui->music_view ^= 1;
        ui->album_sel = ui->music_album;
        sound_play(ui->music_view ? SND_OPEN : SND_CLOSE);
    }
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
            sound_play(SND_MUSIC_ACT);
        }
        ui->seek_dir = 0;
        ui->seeking = 0;
    }
    if (in->pressed & PSP_CTRL_SQUARE) { /* blanda – gäller från nästa låt */
        g_shuffle ^= 1;
        config_save(g_data_dir);
        ui_toast(ui, L(g_shuffle ? S_SHUFFLE_ON : S_SHUFFLE_OFF));
        sound_play(g_shuffle ? SND_TOGGLE : SND_TOGGLE_OFF);
    }
    u32 confirm = (ui->confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS) | PSP_CTRL_START;
    if (in->pressed & confirm) {
        if (ui->music_view) { /* öppnar albumet (0 = alla låtar) */
            ui->music_album = ui->album_sel;
            ui->music_view = 0;
            music_range(ui, &lo, &hi);
            ui->music_sel = ui->music_target = lo;
            ui->music_scroll = lo;
            sound_play(SND_SELECT);
        } else if (playing && ui->music_sel == music_current()) {
            music_toggle_pause();
            sound_play(SND_MUSIC_ACT);
        } else {
            music_play(ui->music_sel);
            sound_play(SND_MUSIC_ACT);
        }
    }
    if (ui->music_sel < ui->music_target)
        ui->music_target = ui->music_sel;
    if (ui->music_sel > ui->music_target + MROWS - 1)
        ui->music_target = ui->music_sel - (MROWS - 1);
    ui->music_scroll = approach(ui->music_scroll, ui->music_target, 14.0f, dt);
    if (ui->album_sel < ui->album_target)
        ui->album_target = ui->album_sel;
    if (ui->album_sel > ui->album_target + MROWS - 1)
        ui->album_target = ui->album_sel - (MROWS - 1);
    ui->album_scroll = approach(ui->album_scroll, ui->album_target, 14.0f, dt);
}

/* --- bildfliken (TAB_PICTURES) --- */

#define MAX_PICS 500
typedef struct {
    char path[160];
    char title[64];
    char sub[64];
} PicItem;

/* Listorna som visas (bara huvudtråden rör dem) och listorna som bakgrundstråden fyller */
static PicItem *s_pics, *s_scan_pics;
static int s_npics, s_pics_cap, s_scan_npics, s_scan_pics_cap;
static volatile int s_media_quit; /* ui_free: bakgrundsskanningen ska sluta */

static int cmp_pic(const void *a, const void *b)
{
    const PicItem *x = a, *y = b;
    int c = strcasecmp(x->sub, y->sub);
    return c ? c : strcasecmp(x->title, y->title);
}

static void add_pic(const char *path, const char *name, const char *folder, const char *dot)
{
    if (s_scan_npics >= MAX_PICS)
        return;
    if (s_scan_npics >= s_scan_pics_cap) {
        int ncap = s_scan_pics_cap ? s_scan_pics_cap * 2 : 32;
        PicItem *p = realloc(s_scan_pics, ncap * sizeof(*s_scan_pics));
        if (!p)
            return;
        s_scan_pics = p;
        s_scan_pics_cap = ncap;
    }
    PicItem *it = &s_scan_pics[s_scan_npics++];
    snprintf(it->path, sizeof(it->path), "%s", path);
    int stem = (int)(dot - name);
    if (stem >= (int)sizeof(it->title))
        stem = sizeof(it->title) - 1;
    snprintf(it->title, sizeof(it->title), "%.*s", stem, name);
    char ext[8];
    int k = 0;
    for (const char *e = dot + 1; *e && k < 5; e++)
        ext[k++] = (*e >= 'a' && *e <= 'z') ? *e - 32 : *e;
    ext[k] = 0;
    if (folder && folder[0])
        snprintf(it->sub, sizeof(it->sub), "%s  ·  %s", folder, ext);
    else
        snprintf(it->sub, sizeof(it->sub), "%s", ext);
}

static void scan_pic_dir(const char *dir, const char *folder, int depth)
{
    SceUID d = sceIoDopen(dir);
    if (d < 0)
        return;
    SceIoDirent ent;
    char path[160];
    memset(&ent, 0, sizeof(ent));
    while (!s_media_quit && sceIoDread(d, &ent) > 0 && s_scan_npics < MAX_PICS) {
        if (ent.d_name[0] != '.' && snprintf(path, sizeof(path), "%s/%s", dir, ent.d_name) < (int)sizeof(path)) {
            if (FIO_S_ISDIR(ent.d_stat.st_mode)) {
                if (depth < 2)
                    scan_pic_dir(path, ent.d_name, depth + 1);
            } else {
                const char *dot = strrchr(ent.d_name, '.');
                if (dot && dot != ent.d_name &&
                    (!strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg") ||
                     !strcasecmp(dot, ".png") || !strcasecmp(dot, ".bmp")))
                    add_pic(path, ent.d_name, folder, dot);
            }
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(d);
}

static void scan_pics(void)
{
    static const char *const dirs[] = { "PICTURE", "PSP/PHOTO", "PSP/SCREENSHOT" };
    for (int d = 0; d < 2; d++) {
        if (d == 0 && sys_ms_is_ef())
            continue;
        const char *dev = d == 0 ? "ms0:" : "ef0:";
        for (int k = 0; k < 3; k++) {
            char root[64];
            snprintf(root, sizeof(root), "%s/%s", dev, dirs[k]);
            scan_pic_dir(root, k == 0 ? "" : (k == 1 ? "PHOTO" : "SCREENSHOT"), 0);
        }
    }
    if (s_scan_npics > 1)
        qsort(s_scan_pics, s_scan_npics, sizeof(*s_scan_pics), cmp_pic);
}

int ui_pic_count(void) { return s_npics; }
const char *ui_pic_path(int i) { return i >= 0 && i < s_npics ? s_pics[i].path : ""; }

static void set_pic_wallpaper(Ui *ui, const char *path)
{
    if (strlen(path) >= sizeof(g_wallpaper)) {
        sound_play(SND_ERROR);
        return;
    }
    snprintf(g_wallpaper, sizeof(g_wallpaper), "%s", path);
    config_save(g_data_dir);
    ui_toast(ui, L(S_WALLPAPER_SET));
    sound_play(SND_TOAST);
}

static void update_pictures(Ui *ui, const UiInput *in, float dt)
{
    int n = s_npics;
    if (ui->pic_sel >= n)
        ui->pic_sel = n > 0 ? n - 1 : 0;
    if (!n) {
        ui->pic_viewing = 0;
        if (ui->pic_tex) {
            tex_free(ui->pic_tex);
            ui->pic_tex = NULL;
            ui->pic_tex_for = -1;
        }
        u32 back_btn = ui->confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;
        if (ui->intro >= 1 && (in->pressed & back_btn)) {
            switch_tab(ui, TAB_MEDIA);
            sound_play(SND_BACK);
        }
        return;
    }

    if (ui->pic_viewing) {
        int dir = (in->pressed & PSP_CTRL_RIGHT) ? 1 : (in->pressed & PSP_CTRL_LEFT) ? -1 : 0;
        if (dir) {
            ui->pic_sel = (ui->pic_sel + dir + n) % n;
            ui->pic_still = 1.0f;
            sound_play(SND_TILE);
        }
        if (in->pressed & PSP_CTRL_SQUARE)
            set_pic_wallpaper(ui, s_pics[ui->pic_sel].path);
        else if (in->pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_CROSS)) {
            ui->pic_viewing = 0;
            sound_play(SND_CLOSE);
        }
    } else {
        u32 back_btn = ui->confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;
        if (ui->intro >= 1 && (in->pressed & back_btn)) {
            switch_tab(ui, TAB_MEDIA);
            sound_play(SND_BACK);
            return;
        }
        UiInput vert = *in;
        vert.held &= ~(PSP_CTRL_LEFT | PSP_CTRL_RIGHT);
        vert.lx = 128;
        int move = step_input(ui, &vert, dt, 1);
        if (move) {
            int next = step_wrap(ui, ui->pic_sel, move / 2, n);
            if (next != ui->pic_sel) {
                ui->pic_sel = next;
                ui->pic_still = 0;
                sound_play(SND_MOVE);
            }
        }
        if (ui->intro >= 1) {
            u32 confirm = (ui->confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS) | PSP_CTRL_START;
            if (in->pressed & confirm) {
                ui->pic_viewing = 1;
                ui->pic_still = 1.0f;
                sound_play(SND_OPEN);
            } else if (in->pressed & PSP_CTRL_SQUARE) {
                set_pic_wallpaper(ui, s_pics[ui->pic_sel].path);
            }
        }
    }

    if (ui->pic_sel < ui->pic_target)
        ui->pic_target = ui->pic_sel;
    if (ui->pic_sel > ui->pic_target + MROWS - 1)
        ui->pic_target = ui->pic_sel - (MROWS - 1);
    ui->pic_scroll = approach(ui->pic_scroll, ui->pic_target, 14.0f, dt);

    ui->pic_still += dt;
    if (ui->pic_tex_for != ui->pic_sel && (ui->pic_still >= 0.14f || ui->pic_viewing)) {
        tex_free(ui->pic_tex);
        ui->pic_tex = art_load_wallpaper(s_pics[ui->pic_sel].path);
        ui->pic_tex_for = ui->pic_sel;
        ui->pic_fade = 0;
    }
    if (ui->pic_tex)
        ui->pic_fade = clampf(ui->pic_fade + dt * 5.0f, 0, 1);
}

/* --- videofliken (TAB_VIDEO) och sökfliken (TAB_SEARCH) --- */

#define MAX_VIDS 300
#define VID_PREVIEW 10000 /* förhandsvisningens index för video i (spelen har 0..antal-1) */
/* MP4 spelas bara i XMB (issue #38) – psp5 visar den med sin .THM-bild. UMD Video (issue #27):
 * ISO:n monteras och spelas i XMB, en skiva i enheten spelas också där. */
enum { VID_PMF, VID_MP4, VID_UMD_ISO, VID_UMD_DISC };
typedef struct {
    Entry entry;
    char title[72];
    char sub[48];
    u8 kind; /* VID_* */
} VidItem;

static VidItem *s_vids, *s_scan_vids;
static int s_nvids, s_vids_cap, s_scan_nvids, s_scan_vids_cap;

static void add_vid_item(const Entry *e, const char *title, const char *sub, int kind)
{
    if (s_scan_nvids >= MAX_VIDS)
        return;
    if (s_scan_nvids >= s_scan_vids_cap) {
        int ncap = s_scan_vids_cap ? s_scan_vids_cap * 2 : 32;
        VidItem *p = realloc(s_scan_vids, ncap * sizeof(*s_scan_vids));
        if (!p)
            return;
        s_scan_vids = p;
        s_scan_vids_cap = ncap;
    }
    VidItem *it = &s_scan_vids[s_scan_nvids++];
    it->entry = *e;
    snprintf(it->title, sizeof(it->title), "%s", title);
    snprintf(it->sub, sizeof(it->sub), "%s", sub);
    it->kind = kind;
}

static void scan_vid_dir(const char *dir, const char *folder, int depth)
{
    SceUID d = sceIoDopen(dir);
    if (d < 0)
        return;
    SceIoDirent ent;
    char path[160];
    memset(&ent, 0, sizeof(ent));
    while (!s_media_quit && sceIoDread(d, &ent) > 0 && s_scan_nvids < MAX_VIDS) {
        if (ent.d_name[0] != '.' && snprintf(path, sizeof(path), "%s/%s", dir, ent.d_name) < (int)sizeof(path)) {
            if (FIO_S_ISDIR(ent.d_stat.st_mode)) {
                if (depth < 2)
                    scan_vid_dir(path, ent.d_name, depth + 1);
            } else if (ent.d_stat.st_size > 2048) {
                const char *dot = strrchr(ent.d_name, '.');
                int mp4 = dot && !strcasecmp(dot, ".mp4");
                if (dot && dot != ent.d_name && (mp4 || !strcasecmp(dot, ".pmf") || !strcasecmp(dot, ".mps"))) {
                    Entry e;
                    memset(&e, 0, sizeof(e));
                    e.type = ENTRY_HOMEBREW;
                    snprintf(e.path, sizeof(e.path), "%s", path);
                    e.vid_offset = 0;
                    e.vid_size = mp4 ? 0 : (u32)ent.d_stat.st_size; /* 0: förhandsvisningen rör den inte */
                    int stem = (int)(dot - ent.d_name);
                    if (stem >= (int)sizeof(e.title))
                        stem = sizeof(e.title) - 1;
                    snprintf(e.title, sizeof(e.title), "%.*s", stem, ent.d_name);
                    char sub[48];
                    snprintf(sub, sizeof(sub), "%s  ·  %s", folder && folder[0] ? folder : "VIDEO", mp4 ? "MP4" : "PMF");
                    add_vid_item(&e, e.title, sub, mp4 ? VID_MP4 : VID_PMF);
                }
            }
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(d);
}

/* UMD Video-ISO:er i <dev>/ISO/VIDEO (som ARK:s XMB-stöd, core/vshctrl/virtual_mp4.c: bara .iso).
 * Titeln och ikonen ur skivans UMD_VIDEO (eller UMD_AUDIO). ISO:er utan dem hoppas över. */
static void scan_umd_video_isos(const char *dev)
{
    char dir[32];
    snprintf(dir, sizeof(dir), "%s/ISO/VIDEO", dev);
    SceUID d = sceIoDopen(dir);
    if (d < 0)
        return;
    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));
    while (!s_media_quit && sceIoDread(d, &ent) > 0 && s_scan_nvids < MAX_VIDS) {
        const char *dot = strrchr(ent.d_name, '.');
        Entry e;
        memset(&e, 0, sizeof(e));
        if (FIO_S_ISDIR(ent.d_stat.st_mode) || !dot || dot == ent.d_name || strcasecmp(dot, ".iso") ||
            snprintf(e.path, sizeof(e.path), "%s/%s", dir, ent.d_name) >= (int)sizeof(e.path)) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        IsoReader r;
        if (iso_open(&r, e.path) >= 0) {
            static const char *const roots[2] = { "UMD_VIDEO", "UMD_AUDIO" };
            for (int k = 0; k < 2; k++) {
                char part[40];
                IsoFile f;
                snprintf(part, sizeof(part), "%s/PLAYLIST.UMD", roots[k]);
                if (!iso_find(&r, part, &f))
                    continue;
                e.type = ENTRY_ISO; /* ikonen läses ur ISO:n (art_load_icon) */
                int stem = (int)(dot - ent.d_name);
                if (stem >= (int)sizeof(e.title))
                    stem = sizeof(e.title) - 1;
                snprintf(e.title, sizeof(e.title), "%.*s", stem, ent.d_name);
                snprintf(part, sizeof(part), "%s/PARAM.SFO", roots[k]);
                u32 sfo_size = 0;
                u8 *sfo = iso_load(&r, part, &sfo_size, 64 * 1024);
                if (sfo) {
                    entry_read_sfo(&e, sfo, sfo_size); /* en tom TITLE behåller filnamnet */
                    free(sfo);
                }
                snprintf(part, sizeof(part), "%s/ICON0.PNG", roots[k]);
                if (iso_find(&r, part, &f)) {
                    e.icon_offset = f.offset;
                    e.icon_size = f.size;
                }
                const ScePspDateTime *t = &ent.d_stat.sce_st_mtime;
                e.file_size = (u32)ent.d_stat.st_size;
                e.file_time = ((t->year - 1980) << 26) | (t->month << 22) | (t->day << 17) | (t->hour << 12) |
                              (t->minute << 6) | t->second; /* ikonens cache följer filen */
                add_vid_item(&e, e.title, "ISO/VIDEO  ·  UMD Video", VID_UMD_ISO);
                break;
            }
            iso_close(&r);
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(d);
}

static void scan_videos(void)
{
    /* som XMB:s Video: VIDEO och MP_ROOT (100MNV01, 101ANV01) */
    static const char *const dirs[] = { "VIDEO", "PSP/VIDEO", "MP_ROOT" };
    for (int d = 0; d < 2; d++) {
        if (d == 0 && sys_ms_is_ef())
            continue;
        const char *dev = d == 0 ? "ms0:" : "ef0:";
        for (int k = 0; k < 3; k++) {
            char root[64];
            snprintf(root, sizeof(root), "%s/%s", dev, dirs[k]);
            scan_vid_dir(root, "VIDEO", 0);
        }
        scan_umd_video_isos(dev);
    }
    /* UMD Video-skivan i enheten (umd.c har redan läst titeln) */
    char title[128];
    if (s_scan_nvids < MAX_VIDS && umd_video(title, sizeof(title))) {
        Entry e;
        memset(&e, 0, sizeof(e));
        e.type = ENTRY_UMD;
        snprintf(e.path, sizeof(e.path), "%s", UMD_ROOT);
        snprintf(e.title, sizeof(e.title), "%s", title);
        add_vid_item(&e, e.title, "UMD  ·  UMD Video", VID_UMD_DISC);
    }
}

/* --- bilder och videor läses in i en egen tråd ---
 * Katalogläsningen på huvudtråden fick flikbytet med L/R (Media) och inställningarna att hänga
 * på PSP Go med ARK 5.2.0 (issue #41). Tråden fyller s_scan_* efter musikskanningen – en
 * katalogläsning i taget – och huvudtråden byter in listorna i media_poll när den är klar. */
static SceUID s_media_th = -1;
static volatile int s_media_done; /* tråden är klar: s_scan_* väntar på huvudtråden */
static int s_media_loaded;        /* listorna har lästs in minst en gång sedan start */

static int media_thread(SceSize args, void *argp)
{
    while (!s_media_quit && music_scanning())
        sceKernelDelayThread(100 * 1000);
    scan_pics();
    scan_videos();
    s_media_done = 1;
    return 0;
}

static void media_thread_end(void)
{
    if (s_media_th < 0)
        return;
    sceKernelWaitThreadEnd(s_media_th, NULL);
    sceKernelDeleteThread(s_media_th);
    s_media_th = -1;
}

/* Varje bildruta: byter in färdiga listor och startar en ny skanning när det behövs */
static void media_poll(const Ui *ui)
{
    if (s_media_done) {
        media_thread_end();
        s_media_done = 0;
        s_media_loaded = 1;
        /* spelens ICON1.PMF-videor listas inte – Videor är för egna filer (issue #43) */
        free(s_pics);
        s_pics = s_scan_pics;
        s_npics = s_scan_npics;
        s_pics_cap = s_scan_pics_cap;
        free(s_vids);
        s_vids = s_scan_vids;
        s_nvids = s_scan_nvids;
        s_vids_cap = s_scan_vids_cap;
        s_scan_pics = NULL;
        s_scan_vids = NULL;
        s_scan_npics = s_scan_pics_cap = s_scan_nvids = s_scan_vids_cap = 0;
    }
    static int again, umd_gen = -1;
    again |= osd_take_screenshot_done(); /* en ny skärmbild ska synas i Bilder */
    if (umd_gen != umd_video_generation()) { /* UMD Video-skivan sattes i eller togs ut */
        if (umd_gen >= 0)
            again = 1;
        umd_gen = umd_video_generation();
    }
    if (s_media_th < 0 && (!s_media_loaded || again)) {
        again = 0;
        s_media_quit = 0;
        s_media_th = sceKernelCreateThread("psp5_media", media_thread, 0x30, 0x4000, PSP_THREAD_ATTR_USER, NULL);
        if (s_media_th >= 0 && sceKernelStartThread(s_media_th, 0, NULL) < 0) {
            sceKernelDeleteThread(s_media_th);
            s_media_th = -1;
        }
        if (s_media_th < 0)
            s_media_loaded = 1; /* ingen tråd: tomma listor hellre än ett nytt försök varje bildruta */
    }
}

/* Avslutar skanningen och glömmer listorna – nästa start läser in dem på nytt (t.ex. efter USB) */
static void media_free(void)
{
    s_media_quit = 1;
    media_thread_end();
    s_media_done = s_media_loaded = 0;
    free(s_scan_pics);
    free(s_scan_vids);
    s_scan_pics = NULL;
    s_scan_vids = NULL;
    s_scan_npics = s_scan_pics_cap = s_scan_nvids = s_scan_vids_cap = 0;
}

static int media_loading(void) { return !s_media_loaded; }

static void update_videos(Ui *ui, const UiInput *in, float dt)
{
    int n = s_nvids;
    if (ui->vid_sel >= n)
        ui->vid_sel = n > 0 ? n - 1 : 0;
    u32 back_btn = ui->confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;
    if (!n) {
        ui->vid_viewing = 0;
        if (ui->intro >= 1 && (in->pressed & back_btn)) {
            switch_tab(ui, TAB_MEDIA);
            sound_play(SND_BACK);
        }
        return;
    }

    if (ui->vid_viewing) {
        int dir = (in->pressed & PSP_CTRL_RIGHT) ? 1 : (in->pressed & PSP_CTRL_LEFT) ? -1 : 0;
        if (dir) {
            ui->vid_sel = (ui->vid_sel + dir + n) % n;
            ui->vid_still = 1.0f;
            sound_play(SND_TILE);
        }
        if (in->pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_CROSS)) {
            ui->vid_viewing = 0;
            sound_play(SND_CLOSE);
        }
    } else {
        if (ui->intro >= 1 && (in->pressed & back_btn)) {
            switch_tab(ui, TAB_MEDIA);
            sound_play(SND_BACK);
            return;
        }
        UiInput vert = *in;
        vert.held &= ~(PSP_CTRL_LEFT | PSP_CTRL_RIGHT);
        vert.lx = 128;
        int move = step_input(ui, &vert, dt, 1);
        if (move) {
            int next = step_wrap(ui, ui->vid_sel, move / 2, n);
            if (next != ui->vid_sel) {
                ui->vid_sel = next;
                ui->vid_still = 0;
                sound_play(SND_MOVE);
            }
        }
        u32 confirm = (ui->confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS) | PSP_CTRL_START;
        if (ui->intro >= 1 && (in->pressed & confirm) && ui->cc_leave <= 0) {
            const VidItem *v = &s_vids[ui->vid_sel];
            if (v->kind == VID_MP4) { /* psp5 kan inte spela MP4 – XMB kan */
                ui_toast(ui, L(S_MP4_IN_XMB));
                sound_play(SND_MSG_TOAST);
            } else if (v->kind == VID_UMD_ISO || v->kind == VID_UMD_DISC) {
                /* spelas i XMB: ISO:n monteras först (main.c, SA_UMD_VIDEO), skivan sitter redan i */
                snprintf(ui->umd_video_path, sizeof(ui->umd_video_path), "%s", v->entry.path);
                ui->cc_action = v->kind == VID_UMD_ISO ? SA_UMD_VIDEO : SA_XMB;
                ui->cc_leave = 0.001f; /* tona till svart, sedan UI_ACTION (update_cc) */
                sound_play(SND_SELECT);
            } else {
                ui->vid_viewing = 1;
                ui->vid_still = 1.0f;
                sound_play(SND_OPEN);
            }
        }
    }

    if (ui->vid_sel < ui->vid_target)
        ui->vid_target = ui->vid_sel;
    if (ui->vid_sel > ui->vid_target + MROWS - 1)
        ui->vid_target = ui->vid_sel - (MROWS - 1);
    ui->vid_scroll = approach(ui->vid_scroll, ui->vid_target, 14.0f, dt);
    ui->vid_still += dt;

    /* MP4: bilden XMB visar, <namn>.THM bredvid filen */
    if (ui->vid_thumb_for != ui->vid_sel && ui->vid_still >= 0.14f) {
        tex_free(ui->vid_thumb);
        ui->vid_thumb = NULL;
        ui->vid_thumb_for = ui->vid_sel;
        const VidItem *v = &s_vids[ui->vid_sel];
        const char *p = v->entry.path, *dot = strrchr(p, '.');
        if (v->kind == VID_MP4 && dot) {
            char thm[sizeof(s_vids[0].entry.path) + 4];
            snprintf(thm, sizeof(thm), "%.*s.THM", (int)(dot - p), p);
            ui->vid_thumb = art_load_wallpaper(thm);
        } else if (v->kind == VID_UMD_ISO && v->entry.icon_size) {
            ui->vid_thumb = art_load_icon(&v->entry, NULL);
        } else if (v->kind == VID_UMD_DISC) {
            ui->vid_thumb = art_load_icon_file(UMD_ROOT "UMD_VIDEO/ICON0.PNG");
        }
    }
}

/* Sök: tangentbord i rutnät (7 per rad: A–Z, mellanslag, ⌫, siffror) + direktfiltrerade spel och appar */
#define SEARCH_KEYS "ABCDEFGHIJKLMNOPQRSTUVWXYZ <1234567890"
#define SEARCH_KEY_COUNT 38
#define SEARCH_COLS 7
#define SEARCH_ROWS 5

static int match_ci(const char *hay, const char *needle)
{
    if (!needle[0])
        return 1;
    int nlen = strlen(needle);
    for (const char *p = hay; *p; p++)
        if (!strncasecmp(p, needle, nlen))
            return 1;
    return 0;
}

static int collect_search(const Ui *ui, int *out, int max_out)
{
    const LoaderView *v = &ui->views[TAB_LIBRARY];
    int n = 0;
    for (int p = 0; p < v->count && n < max_out; p++) {
        const Entry *e = &ui->list->items[v->idx[p]];
        if (match_ci(e->title, ui->search_query) || match_ci(e->game_id, ui->search_query))
            out[n++] = v->idx[p];
    }
    return n;
}

static void update_search(Ui *ui, const UiInput *in, float dt)
{
    int matches[512];
    int n = collect_search(ui, matches, 512);
    if (ui->search_sel >= n)
        ui->search_sel = n > 0 ? n - 1 : 0;

    int move = step_input(ui, in, dt, 1);
    if (ui->search_focus == 0) {
        /* ← → ↑ ↓ i rutnätet; → från högerkanten går till träffarna */
        int k = ui->search_key, col = k % SEARCH_COLS, next = k;
        if (move == -1 && col > 0)
            next = k - 1;
        else if (move == 1 && col < SEARCH_COLS - 1 && k + 1 < SEARCH_KEY_COUNT)
            next = k + 1;
        else if (move == 1 && n > 0) {
            ui->search_focus = 1;
            sound_play(SND_MOVE);
        } else if (move == -2 && k >= SEARCH_COLS)
            next = k - SEARCH_COLS;
        else if (move == 2 && k / SEARCH_COLS < (SEARCH_KEY_COUNT - 1) / SEARCH_COLS)
            next = k + SEARCH_COLS < SEARCH_KEY_COUNT ? k + SEARCH_COLS : SEARCH_KEY_COUNT - 1; /* kortare sista rad */
        if (next != k) {
            ui->search_key = next;
            sound_play(SND_MOVE);
        }
    } else {
        if (move == -1) {
            ui->search_focus = 0;
            sound_play(SND_MOVE);
        } else if ((move == -2 || move == 2) && n > 0) {
            int next = step_wrap(ui, ui->search_sel, move / 2, n);
            if (next != ui->search_sel) {
                ui->search_sel = next;
                sound_play(SND_MOVE);
            }
        }
    }

    if (ui->intro >= 1) {
        u32 confirm = (ui->confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS) | PSP_CTRL_START;
        u32 back_btn = ui->confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;
        if (in->pressed & back_btn) {
            switch_tab(ui, ui->search_from);
            sound_play(SND_BACK);
            return;
        } else if (in->pressed & PSP_CTRL_SQUARE) {
            int len = strlen(ui->search_query);
            if (len > 0) {
                ui->search_query[len - 1] = 0;
                ui->search_sel = 0;
                ui->search_target = 0;
                sound_play(SND_BACK);
            } else {
                sound_play(SND_ERROR);
            }
        } else if (in->pressed & confirm) {
            if (ui->search_focus == 0) {
                char ch = SEARCH_KEYS[ui->search_key];
                int len = strlen(ui->search_query);
                if (ch == '<') {
                    if (len > 0) {
                        ui->search_query[len - 1] = 0;
                        sound_play(SND_BACK);
                    } else {
                        sound_play(SND_ERROR);
                    }
                } else if (len + 1 < (int)sizeof(ui->search_query)) {
                    ui->search_query[len] = ch;
                    ui->search_query[len + 1] = 0;
                    sound_play(SND_TYPE);
                } else {
                    sound_play(SND_ERROR);
                }
                ui->search_sel = 0;
                ui->search_target = 0;
            } else if (n > 0) {
                if (ui->can_launch) {
                    int idx = matches[ui->search_sel];
                    ui->search_launch_idx = idx;
                    ui->launch = 0;
                    if (s_meta) {
                        meta(idx)->played = ++s_played_max;
                        meta_save(ui->list, META_PLAYED);
                    }
                    sound_play(SND_LAUNCH);
                } else {
                    ui_toast(ui, L(S_NO_ARK));
                    sound_play(SND_ERROR);
                }
            }
        }
    }

    if (ui->search_sel < ui->search_target)
        ui->search_target = ui->search_sel;
    if (ui->search_sel > ui->search_target + SEARCH_ROWS - 1)
        ui->search_target = ui->search_sel - (SEARCH_ROWS - 1);
    ui->search_scroll = approach(ui->search_scroll, ui->search_target, 14.0f, dt);
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
        /* inga kategorimappar: SELECT öppnar Control Center direkt */
        ui_open_cc(ui);
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
    if ((move == 2 || move == -2) && s_npick > 0) {
        int next = step_wrap(ui, ui->cat_sel, move / 2, s_npick);
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
    u32 back = ui->confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;
    if (in->pressed & ok) {
        ui->cat_open = 0;
        if (s_pick[ui->cat_sel] != ui->cat[ui->tab]) {
            ui->cat[ui->tab] = s_pick[ui->cat_sel];
            ui_relist(ui, 0, 0); /* markeringen står kvar om kortet finns i kategorin */
            ui->pos = ui->sel[ui->tab] + row_entry_offset(ui->tab, 0);
            ui->tab_anim = 0;
        }
        sound_play(SND_SELECT);
    } else if (in->pressed & (back | PSP_CTRL_SELECT)) { /* SELECT igen stänger (issue #39) */
        ui->cat_open = 0;
        sound_play(SND_CLOSE);
    }
}

/* --- Control Center (HOME eller SELECT) --- */

/* [medhatk] Added CC_STANDBY to Control Center bar between Sleep and USB */
enum { CC_MUSIC, CC_SCREENSHOT, CC_SLEEP, CC_STANDBY, CC_USB, CC_FILES, CC_SETTINGS, CC_XMB, CC_RESTART, CC_OFF, CC_COUNT };

static const char *cc_label(int item)
{
    switch (item) {
    case CC_MUSIC:      return L(music_state() == MUSIC_PLAYING ? S_PAUSE : S_PLAY);
    case CC_SCREENSHOT: return L(S_SCREENSHOT);
    case CC_SLEEP:      return L(S_SLEEP);
    case CC_STANDBY:    return L(S_STANDBY); /* [medhatk] "Standby Screen" label */
    case CC_USB:        return L(S_USB_MODE);
    case CC_FILES:      return L(S_FILES);
    case CC_SETTINGS:   return L(S_SETTINGS);
    case CC_XMB:        return L(S_TO_XMB);
    case CC_RESTART:    return L(S_RESTART);
    }
    return L(S_SHUTDOWN);
}

#define CC_LEAVE 0.22f /* sekunder att tona till svart innan USB, filer, viloläge m.fl. */

void ui_open_cc(Ui *ui)
{
    ui->cc_open = 1;
    ui->cc_confirm = 0;
    ui->cc_pos = ui->cc_sel; /* markeringen står där den stod sist, utan att glida dit */
    ui->cc_label = 0;
    ui->menu_open = ui->cat_open = 0;
    stop_arrange(ui); /* en påbörjad flytt sparas som den står */
    sound_play(SND_SIDE_IN);
}

/* UI_SETTINGS / UI_ACTION (ui->cc_action) / UI_NONE */
static int update_cc(Ui *ui, const UiInput *in, float dt)
{
    ui->cc_anim = clampf(ui->cc_anim + (ui->cc_open ? 5.5f : -8) * dt, 0, 1);
    ui->cc_pos = approach(ui->cc_pos, ui->cc_sel, 18.0f, dt);
    ui->cc_label = clampf(ui->cc_label + dt * 7, 0, 1);
    if (ui->cc_leave > 0) {
        /* tona till svart först – sedan byter main.c läge utan att något blinkar */
        ui->cc_leave += dt / CC_LEAVE;
        if (ui->cc_leave < 1)
            return UI_NONE;
        ui->cc_leave = 0;
        ui->cc_open = 0;
        ui->cc_anim = 0;
        return UI_ACTION;
    }
    if (!ui->cc_open)
        return UI_NONE;
    int move = step_input(ui, in, dt, 0);
    if (move) {
        int next = (ui->cc_sel + move + CC_COUNT) % CC_COUNT;
        if (next != ui->cc_sel) {
            ui->cc_sel = next;
            ui->cc_confirm = 0;
            ui->cc_label = 0;
            sound_play(SND_MOVE);
        }
    }
    u32 ok = ui->confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
    u32 back = (ui->confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE) | PSP_CTRL_HOME | PSP_CTRL_SELECT;
    if (in->pressed & back) {
        ui->cc_open = 0;
        sound_play(SND_SIDE_OUT);
        return UI_NONE;
    }
    if (!(in->pressed & ok))
        return UI_NONE;
    /* [medhatk] Slot 3 (CC_STANDBY) maps to 0 here because it transitions to UI_STANDBY rather than SA_* */
    static const int actions[CC_COUNT] = { 0, 0, SA_SUSPEND, 0, SA_USB, SA_FILES, 0, SA_XMB, SA_RESTART, SA_SHUTDOWN };
    switch (ui->cc_sel) {
    case CC_MUSIC:
        if (music_current() >= 0 && music_state() != MUSIC_STOPPED) {
            music_toggle_pause();
            sound_play(SND_MUSIC_ACT);
        } else if (music_count()) {
            music_play(ui->music_sel);
            sound_play(SND_MUSIC_ACT);
        } else {
            sound_play(SND_ERROR);
        }
        return UI_NONE;
    case CC_SCREENSHOT:
        ui->cc_open = 0;
        ui->cc_anim = 0;
        osd_request_screenshot();
        return UI_NONE;
    case CC_STANDBY:
        /* [medhatk] Close Control Center and hand off to main.c standby_mode() loop */
        ui->cc_open = 0;
        ui->cc_anim = 0;
        sound_play(SND_SIDE_OUT);
        return UI_STANDBY;
    case CC_SETTINGS:
        ui->cc_open = 0;
        return UI_SETTINGS;
    case CC_RESTART:
    case CC_OFF:
        if (!ui->cc_confirm) { /* som i inställningarna: ✕ två gånger */
            ui->cc_confirm = 1;
            ui->cc_label = 0;
            sound_play(SND_MSG_TOAST);
            return UI_NONE;
        }
        break;
    }
    ui->cc_confirm = 0;
    ui->cc_action = actions[ui->cc_sel];
    ui->cc_leave = 0.001f;
    sound_play(SND_SELECT);
    return UI_NONE;
}

int ui_update(Ui *ui, const UiInput *in, float dt)
{
    ui->time += dt;
    ui->intro = clampf(ui->intro + dt * 2.2f, 0, 1);
    ui->tab_anim = clampf(ui->tab_anim + dt * 4.0f, 0, 1);
    if (ui->toast_time > 0)
        ui->toast_time -= dt;
    media_poll(ui);

    if (ui->launch >= 0) {
        preview_want(NULL, -1); /* spelets musik ska inte spela över startljudet */
        if (ui->tab == TAB_GAMES || ui->tab == TAB_APPS)
            ui->pos = approach(ui->pos, ui->sel[ui->tab], 12.0f, dt);
        ui->launch += dt / LAUNCH_LEN;
        return ui->launch >= 1 ? UI_LAUNCH : UI_NONE;
    }
    /* alternativmenyn: ✕ väljer, ○ eller □ stänger – resten av gränssnittet får inga knappar */
    ui->menu_anim = clampf(ui->menu_anim + (ui->menu_open ? 7 : -7) * dt, 0, 1);
    static const UiInput none = { 0, 0, 128, 128 };
    /* HOME öppnar Control Center var man än är (utom under startanimationen) och stänger
     * andra paneler – före dem, så att ingen av dem sväljer knappen */
    if ((in->pressed & PSP_CTRL_HOME) && !ui->cc_open && ui->intro >= 1) {
        ui_open_cc(ui);
        in = &none;
    }
    if (ui->menu_open) {
        u32 ok = ui->confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
        u32 back = (ui->confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE) | PSP_CTRL_SQUARE | PSP_CTRL_TRIANGLE;
        int move = step_input(ui, in, dt, 1);
        int items = menu_count(ui);
        ui->menu_rows = items;
        if ((move == 2 || move == -2) && items > 1) {
            ui->menu_sel = (ui->menu_sel + move / 2 + items) % items;
            sound_play(SND_MOVE);
        }
        if (in->pressed & ok) {
            menu_choose(ui);
            if (ui->want_info) {
                ui->want_info = 0;
                return UI_INFO;
            }
        } else if (in->pressed & back) {
            ui->menu_open = 0;
            sound_play(SND_CLOSE);
        }
        in = &none;
    }
    /* Flytta: ← → flyttar kortet, ✕ / ○ / □ klart */
    if (ui->moving) {
        int move = step_input(ui, in, dt, 0);
        if (move)
            arrange_step(ui, move);
        if (in->pressed & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE | PSP_CTRL_SQUARE)) {
            stop_arrange(ui);
            sound_play(SND_SELECT);
        }
        in = &none;
    }
    int cc_was_open = ui->cc_open || ui->cc_leave > 0; /* på väg ut (t.ex. UMD Video): inga knappar */
    int cc = update_cc(ui, in, dt);
    if (cc != UI_NONE)
        return cc;
    if (cc_was_open)
        in = &none;
    int cat_was_open = ui->cat_open;
    update_categories(ui, in, dt);
    if (cat_was_open)
        in = &none;
    if ((in->pressed & PSP_CTRL_SELECT) && ui->intro >= 1) {
        if (ui->tab == TAB_GAMES || ui->tab == TAB_APPS || ui->tab == TAB_LIBRARY)
            open_categories(ui);
        else
            ui_open_cc(ui);
        in = &none;
    }
    if (ui->tab == TAB_PICTURES && ui->pic_viewing && in != &none) {
        update_pictures(ui, in, dt);
        in = &none;
    }
    if (ui->tab == TAB_VIDEO && ui->vid_viewing && in != &none) {
        update_videos(ui, in, dt);
        in = &none;
    }

    /* L och R bläddrar direkt genom toppradens 5 val:
     * Games (0) <-> Apps (1) <-> Library (2) <-> Music (3) <-> Settings (4) */
    if ((in->pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) && in != &none && ui->intro >= 1) {
        int sec = 0;
        if (ui->tab == TAB_APPS)
            sec = 1;
        else if (ui->tab == TAB_LIBRARY)
            sec = 2;
        else if (ui->tab == TAB_MUSIC || ui->tab == TAB_MEDIA || ui->tab == TAB_PICTURES || ui->tab == TAB_VIDEO)
            sec = 3;
        int step = (in->pressed & PSP_CTRL_RTRIGGER) ? 1 : -1;
        int next_sec = (sec + step + 5) % 5;
        if (next_sec == 4)
            return UI_SETTINGS;
        static const int sec_tabs[4] = { TAB_GAMES, TAB_APPS, TAB_LIBRARY, TAB_MUSIC };
        switch_tab(ui, sec_tabs[next_sec]);
    }

    int overlay = in == &none; /* en panel är öppen: knapprepetitionen tillhör den */
    int tab_before = ui->tab;
    if (ui->tab == TAB_MUSIC && !overlay)
        update_music(ui, in, dt);
    if (ui->tab == TAB_PICTURES && !overlay)
        update_pictures(ui, in, dt);
    if (ui->tab == TAB_VIDEO && !overlay)
        update_videos(ui, in, dt);
    if (ui->tab == TAB_SEARCH && !overlay)
        update_search(ui, in, dt);
    if (ui->tab != tab_before) /* t.ex. ○ i sökningen: samma tryck ska inte gälla i fliken man kom till */
        in = &none;

    u32 confirm = (ui->confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS) | PSP_CTRL_START;
    u32 back_btn = ui->confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;

    /* Media-raden: 3 kort (Music, Pictures, Videos) */
    if (ui->tab == TAB_MEDIA && !overlay) {
        int move_m = step_input(ui, in, dt, 0);
        if (move_m) {
            int next = (ui->sel[TAB_MEDIA] + move_m + 3) % 3;
            if (next != ui->sel[TAB_MEDIA]) {
                int wrapped = abs(next - ui->sel[TAB_MEDIA]) > 1;
                ui->sel[TAB_MEDIA] = next;
                if (wrapped)
                    ui->pos = next + (move_m > 0 ? -0.7f : 0.7f);
                ui->still = 0;
                sound_play(wrapped ? SND_BUMPER : SND_TILE);
            }
        }
        if ((in->pressed & confirm) && ui->intro >= 1) {
            static const int media_tabs[3] = { TAB_MUSIC, TAB_PICTURES, TAB_VIDEO };
            switch_tab(ui, media_tabs[(int)clampf(ui->sel[TAB_MEDIA], 0, 2)]);
        }
    }

    int count = ui->tab < TAB_MUSIC ? view(ui)->count : 0;
    int is_row_tab = ui->tab == TAB_GAMES || ui->tab == TAB_APPS;
    int row_count = is_row_tab && count ? count + 1 : count; /* +1 för Spelbibliotekskortet längst till höger */
    int *sel = &ui->sel[ui->tab];
    int grid = ui->tab == TAB_LIBRARY;
    if (grid && !overlay && (in->pressed & back_btn) && ui->intro >= 1) {
        switch_tab(ui, TAB_GAMES);
        sound_play(SND_BACK);
    }
    /* △ öppnar inställningarna, som före 2.1.0 (issue #34) – musikfliken har △ för album */
    if ((is_row_tab || ui->tab == TAB_MEDIA || grid) && (in->pressed & PSP_CTRL_TRIANGLE) &&
        ui->intro >= 1 && ui->launch < 0)
        return UI_SETTINGS;
    if ((is_row_tab || grid) && (in->pressed & PSP_CTRL_SQUARE) && ui_selected(ui) >= 0 &&
        ui->intro >= 1 && ui->launch < 0) {
        ui->menu_open = 1;
        ui->menu_sel = 0;
        ui->menu_rows = menu_count(ui);
        sound_play(SND_OPEN);
    }
    int move = ui->tab >= TAB_MUSIC || overlay ? 0 : step_input(ui, in, dt, grid);
    if (move && row_count) {
        int next = *sel;
        if (move == 2 || move == -2) {
            int rows = (row_count + GRID_COLS - 1) / GRID_COLS;
            int r = *sel / GRID_COLS, c = *sel % GRID_COLS;
            r = step_wrap(ui, r, move > 0 ? 1 : -1, rows);
            next = r * GRID_COLS + c;
            if (next >= row_count)
                next = row_count - 1;
        } else {
            next = step_wrap(ui, *sel, move, row_count);
        }
        if (next != *sel) {
            int wrapped = !grid && abs(next - *sel) > 1;
            *sel = next;
            if (wrapped)
                ui->pos = next + (move > 0 ? -0.7f : 0.7f);
            ui->still = 0;
            sound_play(wrapped ? SND_BUMPER : (grid ? SND_MOVE : SND_TILE));
        }
    }
    ui->still += dt;

    /* START eller ✕ startar valt spel – eller öppnar Spelbiblioteket om sista kortet är valt */
    if (ui->tab < TAB_MUSIC && (in->pressed & confirm) && row_count && ui->intro >= 1) {
        if (is_row_tab && *sel == count) {
            switch_tab(ui, TAB_LIBRARY);
        } else if (ui_selected(ui) >= 0) {
            if (ui->can_launch) {
                ui->launch = 0;
                if (s_meta) { /* Senast spelat */
                    meta(ui_selected(ui))->played = ++s_played_max;
                    meta_save(ui->list, META_PLAYED);
                }
                sound_play(SND_LAUNCH);
            } else {
                ui_toast(ui, L(S_NO_ARK));
                sound_play(SND_ERROR);
            }
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

    ui->pos = approach(ui->pos, *sel, is_row_tab ? 11.5f : 13.0f, dt);
    if (fabsf(ui->pos - *sel) < 0.001f)
        ui->pos = *sel;
    if (ui->tab < TAB_MUSIC && *sel < count)
        loader_focus(*sel);
    if (music_needs_cjk())
        font_ensure_cjk(); /* japanska/ryska låttitlar (laddas bara en gång) */
    update_background(ui, dt);
    update_cover(ui, dt);

    /* spelets video och musik, som i XMB – eller vald video i Videospelaren (TAB_VIDEO) */
    int pv = -1;
    int pv_card = 0;
    const Entry *pv_entry = NULL;
    if (ui->tab == TAB_VIDEO && s_nvids > 0 && ui->vid_still >= 0.14f && !ui->cc_open) {
        pv = VID_PREVIEW + ui->vid_sel;
        pv_entry = &s_vids[ui->vid_sel].entry;
    } else if (g_previews && is_row_tab && !overlay && ui->cat_anim <= 0 &&
               ui->cc_anim <= 0 && ui->intro >= 1 && ui->still >= PREVIEW_DELAY) {
        pv = ui_selected(ui);
        if (pv >= 0) {
            pv_entry = &ui->list->items[pv];
            pv_card = 1;
        }
    }
    preview_want_ex(pv_entry, pv, pv_card);
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

    if (ui->label_for != label_key(ui)) {
        fit_label(ui);
        ui->label_anim = 0;
    }
    ui->label_anim = clampf(ui->label_anim + dt * 5.0f, 0, 1);
    return UI_NONE;
}

/* --- ritning --- */

/* Temats yta med ett mjukt ljus uppifrån – syns när spelet saknar bild (eller bilderna är avstängda). */
static u32 mix_rgba(u32 a, u32 b, float t)
{
    t = clampf(t, 0.0f, 1.0f);
    u32 out = 0;
    for (int s = 0; s < 32; s += 8)
        out |= (u32)(lerpf((a >> s) & 0xFF, (b >> s) & 0xFF, t) + 0.5f) << s;
    return out;
}

static float ui_art_alpha(const Ui *ui)
{
    if (g_wallpaper[0])
        return 1.0f;
    if (!ui)
        return 0.0f;
    if (ui->bg_cur)
        return ui->bg_old ? 1.0f : clampf(ui->bg_fade, 0.0f, 1.0f);
    if (ui->bg_old)
        return clampf(1.0f - ui->bg_fade, 0.0f, 1.0f);
    return 0.0f;
}

/* För ljusa teman: ger `light_ink` över temabakgrunden och tonar mot `over_art`
 * när en spelbild eller egen bakgrundsbild ligger bakom. */
static u32 ink_over_art(const Theme *t, u32 light_ink, u32 over_art, float art_a)
{
    if (!t || !t->ink)
        return over_art;
    return art_a > 0.001f ? mix_rgba(light_ink, over_art, art_a) : light_ink;
}

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

static void rot_pt(float cx, float cy, float cs, float sn, float lx, float ly, float *ox, float *oy)
{
    *ox = cx + lx * cs - ly * sn;
    *oy = cy + lx * sn + ly * cs;
}

static void draw_stroke_loop(float cx, float cy, float cs, float sn, const float *pts, int n, float width, u32 color)
{
    float xy[68];
    u32 cols[34];
    if (n > 34)
        n = 34;
    for (int i = 0; i < n; i++) {
        rot_pt(cx, cy, cs, sn, pts[i * 2], pts[i * 2 + 1], &xy[i * 2], &xy[i * 2 + 1]);
        cols[i] = color;
    }
    gfx_stroke(xy, cols, n, width);
}

static void draw_circle_stroke(float cx, float cy, float r, float width, u32 color)
{
    float ring[58];
    u32 cols[29];
    for (int i = 0; i < 29; i++) {
        float a = (float)i * (6.2831853f / 28.0f);
        ring[i * 2] = cx + cosf(a) * r;
        ring[i * 2 + 1] = cy + sinf(a) * r;
        cols[i] = color;
    }
    gfx_stroke(ring, cols, 29, width);
}

/* Tilted 2x2 PlayStation geometric symbols in the lower-right backdrop matching Figma. */
static void draw_ps_watermark(u32 color)
{
    const float ang = -0.244f; /* -14 deg */
    const float cs = cosf(ang), sn = sinf(ang);
    const float bx = 422.0f, by = 170.0f, off = 23.5f, sw = 3.2f;
    float cx, cy;

    /* Top-left: Square (starts/ends on straight top midpoint so stroke join is seamless). */
    rot_pt(bx, by, cs, sn, -off, -off, &cx, &cy);
    static const float sq[26] = {
          0.0f, -13.5f,  10.0f, -13.5f,  12.6f, -12.6f,  13.5f, -10.0f,
         13.5f,  10.0f,  12.6f,  12.6f,  10.0f,  13.5f, -10.0f,  13.5f,
        -12.6f,  12.6f, -13.5f,  10.0f, -13.5f, -10.0f, -12.6f, -12.6f,
          0.0f, -13.5f
    };
    draw_stroke_loop(cx, cy, cs, sn, sq, 13, sw, color);

    /* Top-right: Triangle (starts/ends on straight base midpoint). */
    rot_pt(bx, by, cs, sn, off, -off, &cx, &cy);
    static const float tri[18] = {
          0.0f,  12.0f, -12.5f,  12.0f, -14.2f,  10.4f, -13.5f,   8.0f,
         -1.3f, -13.2f,   1.3f, -13.2f,  13.5f,   8.0f,  14.2f,  10.4f,
          0.0f,  12.0f
    };
    draw_stroke_loop(cx, cy, cs, sn, tri, 9, sw, color);

    /* Bottom-left: Cross. */
    rot_pt(bx, by, cs, sn, -off, off, &cx, &cy);
    static const float x1[4] = { -11.5f, -11.5f,  11.5f, 11.5f };
    static const float x2[4] = {  11.5f, -11.5f, -11.5f, 11.5f };
    draw_stroke_loop(cx, cy, cs, sn, x1, 2, sw, color);
    draw_stroke_loop(cx, cy, cs, sn, x2, 2, sw, color);

    /* Bottom-right: Circle. */
    rot_pt(bx, by, cs, sn, off, off, &cx, &cy);
    draw_circle_stroke(cx, cy, 14.5f, sw, color);
}

void ui_draw_calm_backdrop(const Theme *t, float time, const float accent[3], int allow_wallpaper)
{
    const Theme *cur = t ? t : theme_cur();
    int is_default = (cur == theme_get(0));

    if (allow_wallpaper && g_wallpaper[0]) {
        draw_spotlight(accent, time);
        gfx_gradient_v(0, 0, SCREEN_W, 120, RGBA(0, 0, 0, 145), RGBA(0, 0, 0, 0));
        gfx_gradient_v(0, 160, SCREEN_W, SCREEN_H - 160, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 155));
    } else if (is_default) {
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(9, 13, 19, 255));
    } else {
        theme_backdrop(cur, time, accent, 0, 1);
        if (cur->deco == DECO_ANNIV)
            return; /* inbyggd 30th Anniversary-bakgrund innehåller redan symboler och märke */
        if (!cur->ink)
            gfx_gradient_v(0, 175, SCREEN_W, SCREEN_H - 175, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 95));
    }

    /* Figma ambient flowing waves (broad swells + crisp glowing wave ridges). */
    float xy[48];
    u32 colors[24];
    for (int row = 0; row < 2; row++) {
        for (int p = 0; p < 24; p++) {
            float px = p * (SCREEN_W / 23.0f);
            float tt = px / SCREEN_W;
            float swell = sinf(tt * 4.2f + time * 0.14f + row * 0.9f) * 14.0f;
            float py = 204.0f + row * 15.0f + tt * 18.0f + swell;
            xy[p * 2] = px;
            xy[p * 2 + 1] = py;
            colors[p] = is_default ? RGBA(18, 52, 98, 52 - row * 10)
                      : cur->ink   ? COLOR_ALPHA(cur->ink->panel_deep, 42 - row * 10)
                                   : COLOR_ALPHA(cur->accent, 36 - row * 8);
        }
        gfx_stroke(xy, colors, 24, 18.0f - row * 4.0f);
    }
    for (int line = 0; line < 2; line++) {
        for (int p = 0; p < 24; p++) {
            float px = p * (SCREEN_W / 23.0f);
            float tt = px / SCREEN_W;
            float py = 212.0f + line * 11.0f + sinf(tt * 4.6f + time * 0.18f + line * 1.4f) * (12.0f - line * 2.0f)
                     + tt * 14.0f;
            xy[p * 2] = px;
            xy[p * 2 + 1] = py;
            colors[p] = is_default ? RGBA(56, 128, 214, 52 - line * 14)
                      : cur->ink   ? COLOR_ALPHA(cur->ink->border_hi, 52 - line * 14)
                                   : COLOR_ALPHA(cur->accent, 48 - line * 12);
        }
        gfx_stroke(xy, colors, 24, 2.0f - line * 0.5f);
    }

    u32 watermark = is_default ? RGBA(126, 158, 200, 19)
                  : cur->ink   ? COLOR_ALPHA(cur->ink->shadow, 26)
                               : COLOR_ALPHA(cur->accent, 22);
    draw_ps_watermark(watermark);
}

static void draw_background(Ui *ui)
{
    int calm_bg = ui->tab == TAB_GAMES || ui->tab == TAB_APPS || ui->tab == TAB_LIBRARY || ui->tab == TAB_MUSIC;
    int has_art = ui->bg_cur != NULL || ui->bg_old != NULL;

    /* Visa lugn temabakgrund som grund på huvudflikarna, men låt spelets bakgrundsbild (PIC1)
     * tonas in ovanpå när "Spelens bilder i bakgrunden" (g_theme_art) är aktiverat. */
    if (!ui->bg_cur || (ui->bg_fade < 1 && !ui->bg_old)) {
        if (calm_bg)
            ui_draw_calm_backdrop(theme_cur(), ui->time, ui->accent, 1);
        else
            draw_spotlight(ui->accent, ui->time);
    }

    if (has_art) {
        const u32 tint = RGBA(205, 205, 210, 0);
        int fade = (int)(ui->bg_fade * 255);
        if (ui->bg_old)
            gfx_tex(ui->bg_old, 0, 0, SCREEN_W, SCREEN_H, tint | (u32)(ui->bg_cur ? 255 : 255 - fade) << 24);
        if (ui->bg_cur)
            gfx_tex(ui->bg_cur, 0, 0, SCREEN_W, SCREEN_H, tint | (u32)fade << 24);

        /* toningar: uppe (flikar/kort), vänster (text) och nere (fotlist/knappar) */
        gfx_gradient_v(0, 0, SCREEN_W, 120, RGBA(0, 0, 0, 165), RGBA(0, 0, 0, 0));
        gfx_gradient_h(0, 0, 300, SCREEN_H, RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 0));
        gfx_gradient_v(0, 160, SCREEN_W, SCREEN_H - 160, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 175));
    }
}

/* Laddas batteriet sveper fyllningen från nivån upp till fullt om och om igen,
 * och en liten blixt syns till vänster om batteriet. */
/* bolt_gap: extra plats mellan blixten och batteriet (för procent/tid kvar) */
static void draw_battery(float x, float y, int percent, int charging, float time, float a, float bolt_gap,
                         const Theme *t, float art_a)
{
    u32 fg = ink_over_art(t, t->ink ? t->ink->graphite : LIGHT, LIGHT, art_a);
    u32 inside = ink_over_art(t, t->top, DARK, art_a);
    u32 low = ink_over_art(t, t->ink ? t->ink->err : RGBA(255, 95, 85, 255), RGBA(255, 95, 85, 255), art_a);
    gfx_round_rect(x, y, 20, 10, 2.5f, with_alpha(fg, 0.85f * a));
    gfx_round_rect(x + 1.3f, y + 1.3f, 17.4f, 7.4f, 1.8f, with_alpha(inside, a));
    gfx_rect(x + 20, y + 3, 1.8f, 4, with_alpha(fg, 0.85f * a));
    if (percent >= 0) {
        const float full = 14.8f;
        float w = full * percent / 100.0f;
        if (charging) {
            float p = fmodf(time, 1.8f) / 1.8f;
            float sweep = w + (full - w) * smooth(p / 0.8f);
            float fade = 1 - smooth((p - 0.75f) / 0.25f);
            gfx_round_rect(x + 2.6f, y + 2.6f, sweep + 0.4f, 4.8f, 1, with_alpha(fg, 0.5f * fade * a));
        }
        u32 c = percent <= 15 && !charging ? low : fg;
        gfx_round_rect(x + 2.6f, y + 2.6f, w + 0.4f, 4.8f, 1, with_alpha(c, a));
    }
    if (charging) {
        /* 1,4× större än förut: i den gamla storleken blev blixten bara några taggiga pixlar */
        const float s = 1.4f;
        float cx = x - 7 - bolt_gap, cy = y + 5;
        float bolt[12] = { cx + 1.6f * s, cy - 5.2f * s, cx - 2.6f * s, cy + 0.9f * s, cx - 0.2f * s, cy + 0.9f * s,
                           cx - 1.6f * s, cy + 5.2f * s, cx + 2.6f * s, cy - 0.9f * s, cx + 0.2f * s, cy - 0.9f * s };
        gfx_fan(cx, cy, bolt, 6, with_alpha(fg, 0.9f * a));
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

/* blandar två färger (RGB), t = 0 → a, 1 → b, helt ogenomskinlig */
static u32 mix(u32 a, u32 b, float t)
{
    u32 out = 0xFF000000;
    for (int s = 0; s < 24; s += 8)
        out |= (u32)lerpf((a >> s) & 0xFF, (b >> s) & 0xFF, clampf(t, 0, 1)) << s;
    return out;
}

/* Sök- och inställningsikoner uppe till höger i toppraden (som på PS5): förrenderade och
 * kantutjämnade i 16 × 16 (tools/make_icons.ps1 → icons.h), ritade pixel för pixel */
static void draw_icon_wh(const unsigned char *alpha, int w, int h, Texture **tex, float x, float y, u32 c)
{
    if (!*tex && (*tex = tex_create(w, h, GU_PSM_8888))) {
        u32 *px = (*tex)->data;
        for (int row = 0; row < h; row++)
            for (int col = 0; col < w; col++)
                px[row * (*tex)->stride + col] = RGBA(255, 255, 255, alpha[row * w + col]);
        tex_flush(*tex);
    }
    if (*tex)
        gfx_tex_1to1(*tex, x, y, c);
}

static void draw_icon_n(const unsigned char *alpha, int n, Texture **tex, float cx, float cy, u32 c)
{
    draw_icon_wh(alpha, n, n, tex, cx - n / 2.0f, cy - n / 2.0f, c);
}

static void draw_icon(const unsigned char *alpha, Texture **tex, float cx, float cy, u32 c)
{
    draw_icon_n(alpha, ICON_N, tex, cx, cy, c);
}

/* Förrenderade ikoner som även inställningarna använder (kategorier, Control Center). */
void ui_draw_mask_icon(int id, float cx, float cy, u32 c)
{
    static Texture *t[UI_ICON_COUNT];
    static const unsigned char *const masks[UI_ICON_COUNT] = {
        ICON_CAT_PSP5, ICON_CAT_THEME, ICON_CAT_PERF, ICON_CAT_SYSTEM, ICON_CAT_PLUGINS, ICON_CAT_POWER,
        ICON_CC_USB, ICON_CC_GEAR
    };
    if (id < 0 || id >= UI_ICON_COUNT)
        return;
    draw_icon_n(masks[id], id >= UI_ICON_CC_USB ? ICON_CC_N : ICON_N, &t[id], cx, cy, c);
}

static void draw_search_icon(float cx, float cy, u32 c)
{
    static Texture *t;
    draw_icon(ICON_SEARCH, &t, cx, cy, c);
}

static void draw_gear_icon(float cx, float cy, u32 c)
{
    static Texture *t;
    draw_icon(ICON_GEAR, &t, cx, cy, c);
}

/* Skarpa, kantutjämnade 16x16-ikoner från src/icons.h (genererade via tools/make_icons.ps1). */
static void draw_tab_icon(int tab, float cx, float cy, u32 c)
{
    static Texture *t[4];
    static const unsigned char *const icons[4] = {
        ICON_TAB_GAMES, ICON_TAB_APPS, ICON_TAB_LIBRARY, ICON_TAB_MUSIC
    };
    if (tab >= 0 && tab < 4)
        draw_icon(icons[tab], &t[tab], cx, cy, c);
}

void ui_draw_top_bar_for(int active_tab, int in_settings, float a, float anim_time, const Theme *t, float art_a)
{
    if (!t)
        t = theme_cur();
    u32 pill_col = (t == theme_get(0)) ? RGBA(37, 84, 150, (int)(255 * a))
                                       : COLOR_ALPHA(theme_toggle_on(t), (int)(240 * a));
    u32 main_fg = ink_over_art(t, t->ink ? t->ink->text : TEXT_MAIN, TEXT_MAIN, art_a);
    u32 dim_fg = ink_over_art(t, t->ink ? t->ink->text_dim : TEXT_DIM, TEXT_DIM, art_a);

    char clock[16] = "";
    dt_clock_text(clock, sizeof(clock));
    float right_edge = SCREEN_W - 14.0f;
    text_draw(right_edge, 24.5f, 0.74f, 0, with_alpha(main_fg, a), ALIGN_RIGHT, clock);
    float cw = text_width(0.74f, 0, clock);
    float bx = right_edge - cw - 30;
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
    float iw = info[0] ? text_width(0.58f, 0, info) + 5 : 0;
    if (info[0])
        text_draw(bx - 5, 24.0f, 0.58f, 0, with_alpha(dim_fg, a), ALIGN_RIGHT, info);
    draw_battery(bx, 15.0f, level, charging, anim_time, a, iw, t, art_a);
    float status_left = bx - (charging ? 10 : 0) - iw;

    /* Höger: Inställningar (⚙) till vänster om batteriet & klockan */
    float gear_cx = status_left - (in_settings ? 22.0f : 18.0f), icon_cy = 20.0f;
    if (in_settings)
        theme_sel_box(t, gear_cx - 15.0f, 9.0f, 30.0f, 22.0f, 11.0f, pill_col);
    u32 gear_idle = ink_over_art(t, COLOR_ALPHA(t->ink ? t->ink->graphite : TEXT_MAIN, (int)(215 * a)),
                                 RGBA(255, 255, 255, (int)(195 * a)), art_a);
    u32 gear_act = with_alpha(THEME_INK(t, sel_ink, TEXT_MAIN), a);
    draw_gear_icon(gear_cx, icon_cy, in_settings ? gear_act : gear_idle);
    status_left = gear_cx - (in_settings ? 19.0f : 12.0f);

    /* Enhetlig topprad för alla huvudflikar: Games, Apps, Library, Music */
    static const int tabs[4] = { TAB_GAMES, TAB_APPS, TAB_LIBRARY, TAB_MUSIC };
    const char *names[4] = { L(S_TAB_GAMES), L(S_TAB_APPS), L(S_TAB_LIBRARY), L(S_TAB_MUSIC) };
    u32 tab_idle_ic = ink_over_art(t, COLOR_ALPHA(t->ink ? t->ink->text_dim : TEXT_DIM, (int)(240 * a)),
                                   RGBA(209, 213, 219, (int)(230 * a)), art_a);
    u32 tab_idle_tx = ink_over_art(t, COLOR_ALPHA(t->ink ? t->ink->text : TEXT_MAIN, (int)(220 * a)),
                                   RGBA(209, 213, 219, (int)(245 * a)), art_a);
    float x = 14.0f;
    for (int k = 0; k < 4; k++) {
        int active = !in_settings && tabs[k] == active_tab;
        float w = text_width(0.62f, active, names[k]);
        float item_w = active ? 40.0f + w : 28.0f + w;
        if (active) {
            theme_sel_box(t, x, 9.0f, item_w, 22.0f, 11.0f, pill_col);
            u32 act_ic = t->ink ? with_alpha(t->ink->sel_ink, a) : RGBA(255, 255, 255, (int)(255 * a));
            u32 act_tx = with_alpha(THEME_INK(t, sel_ink, TEXT_MAIN), a);
            draw_tab_icon(tabs[k], x + 17.0f, 20.0f, act_ic);
            text_draw(x + 29.0f, 24.5f, 0.62f, 1, act_tx, ALIGN_LEFT, names[k]);
        } else {
            draw_tab_icon(tabs[k], x + 13.0f, 20.0f, tab_idle_ic);
            text_draw(x + 24.0f, 24.5f, 0.62f, 0, tab_idle_tx, ALIGN_LEFT, names[k]);
        }
        x += item_w + 8.0f;
    }

    /* musik som spelas syns som en liten not med titeln i de andra flikarna */
    int cur = music_current();
    if ((in_settings || active_tab != TAB_MUSIC) && cur >= 0 && music_state() != MUSIC_STOPPED) {
        float right = status_left - 8, left = x + 4;
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
            float pulse = music_state() == MUSIC_PLAYING ? 0.75f + 0.25f * sinf(anim_time * 3) : 0.5f;
            float tw = text_width(0.5f, 0, line);
            u32 note_col = ink_over_art(t, t->ink ? t->ink->graphite : LIGHT, LIGHT, art_a);
            draw_note(right - tw - 8, 20.0f, 10, with_alpha(note_col, pulse * a));
            text_draw(right, 24.0f, 0.5f, 0, with_alpha(dim_fg, a), ALIGN_RIGHT, line);
        }
    }
}

void ui_draw_top_bar(int active_tab, int in_settings, float a, float anim_time)
{
    ui_draw_top_bar_for(active_tab, in_settings, a, anim_time, theme_cur(), 0.0f);
}

static void draw_top_bar(Ui *ui)
{
    ui_draw_top_bar_for(ui->tab, 0, ui->intro, ui->time, theme_cur(), ui_art_alpha(ui));
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

static void draw_card(Ui *ui, int list_index, float x, float y, float w, float h, float focus, float alpha, float art_a)
{
    const Theme *th = theme_cur();
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
        u32 cc = th->card; /* temats färg, andas medan bilden laddas */
        gfx_round_rect(x, y, w, h, r, RGBA((cc & 0xFF) + (int)(10 * b), ((cc >> 8) & 0xFF) + (int)(10 * b),
                                           ((cc >> 16) & 0xFF) + (int)(12 * b), (int)(235 * pa)));
        const char *t = ui->list->items[list_index].title;
        char initial[2] = { (t[0] >= 'a' && t[0] <= 'z') ? t[0] - 32 : t[0], 0 };
        if ((u8)initial[0] < 0x80 && initial[0]) {
            u32 init_col = th->ink ? COLOR_ALPHA(th->ink->text, (int)(200 * pa))
                                   : RGBA(205, 208, 220, (int)(200 * pa));
            text_draw(x + w / 2, y + h / 2 + 8 * (w / BIG_W), 1.1f * (w / BIG_W), 0,
                      init_col, ALIGN_CENTER, initial);
        }
    }
    if (shown > 0) {
        int tint = (int)lerpf(200, 255, focus);
        gfx_tex(slot->tex, x, y, w, h, RGBA(tint, tint, tint, (int)(255 * alpha * shown)));
    }
    /* spelets video (ICON1.PMF) över ikonen på valt kort */
    int vx, vy, vw, vh;
    Texture *video = focus > 0.5f && ui->preview_anim > 0
                     ? preview_frame_sub(list_index, &vx, &vy, &vw, &vh) : NULL;
    if (video)
        gfx_tex_sub(video, (float)vx, (float)vy, (float)vw, (float)vh,
                    x, y, w, h, RGBA(255, 255, 255, (int)(255 * alpha * smooth(ui->preview_anim))));

    /* fäst: en liten nål i övre högra hörnet */
    if (meta(list_index)->pinned) {
        float s = w / BIG_W, bx = x + w - 8 * s - 2, by = y + 8 * s + 2;
        gfx_circle(bx, by, 6.5f * s, RGBA(18, 18, 22, (int)(200 * alpha)));
        gfx_circle(bx, by - 1.4f * s, 2.4f * s, RGBA(240, 240, 244, (int)(255 * alpha)));
        gfx_rect(bx - 0.6f * s, by - 0.5f * s, 1.2f * s, 4.2f * s, RGBA(240, 240, 244, (int)(255 * alpha)));
    }

    /* valt kort: tunn ljus ram med ett litet glapp */
    if (focus > 0.01f) {
        u32 ring = ink_over_art(th, COLOR_ALPHA(th->ink ? th->ink->sel_edge : LIGHT, (int)(240 * focus * alpha)),
                                RGBA(240, 240, 244, (int)(235 * focus * alpha)), art_a);
        gfx_ring(x - 4, y - 4, w + 8, h + 8, r + 4, ring);
    }
}

/* PS5 Game Library-kortet: 3×2-rutnät + liten handkontroll med valfri hörnradie. */
static void draw_library_card_r(Ui *ui, float x, float y, float w, float h, float r, float focus, float alpha)
{
    (void)ui;
    const Theme *th = theme_cur();
    float s = w / BIG_W;
    gfx_glow(x + 1, y + 3, w - 2, h - 2, 5 + 4 * focus, RGBA(0, 0, 0, (int)(130 * alpha)));
    u32 cc = th->card;
    u32 fill = th->ink ? COLOR_ALPHA(th->ink->panel, (int)(245 * alpha))
                       : RGBA((cc & 0xFF) + 4, ((cc >> 8) & 0xFF) + 5, ((cc >> 16) & 0xFF) + 8, (int)(238 * alpha));
    gfx_round_rect(x, y, w, h, r, fill);

    float cx = x + w / 2, cy = y + h / 2;
    u32 ic = th->ink ? COLOR_ALPHA(th->ink->graphite, (int)((185 + 65 * focus) * alpha))
                     : RGBA(240, 242, 248, (int)((185 + 65 * focus) * alpha));
    float sq = 6.2f * s, gap = 2.6f * s;
    float gx0 = cx - (3 * sq + 2 * gap) / 2, gy0 = cy - 11.5f * s;
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 3; col++) {
            if (row == 1 && col == 1)
                continue; /* plats för handkontrollen i mitten av nedersta raden */
            gfx_round_rect(gx0 + col * (sq + gap), gy0 + row * (sq + gap), sq, sq, 1.5f * s, ic);
        }
    }
    /* liten PS5-handkontroll nederst i mitten */
    float py = cy + 6.5f * s;
    gfx_round_rect(cx - 6.0f * s, py - 3.6f * s, 12.0f * s, 5.2f * s, 2.3f * s, ic);
    gfx_round_rect(cx - 7.2f * s, py - 2.0f * s, 3.8f * s, 6.2f * s, 1.8f * s, ic);
    gfx_round_rect(cx + 3.4f * s, py - 2.0f * s, 3.8f * s, 6.2f * s, 1.8f * s, ic);

    if (focus > 0.01f)
        gfx_ring(x - 4, y - 4, w + 8, h + 8, r + 4,
                 COLOR_ALPHA(THEME_INK(th, sel_edge, RGBA(240, 240, 244, 255)), (int)(235 * focus * alpha)));
}

static void draw_library_card(Ui *ui, float x, float y, float w, float h, float focus, float alpha)
{
    float r = lerpf(5.0f, BIG_R, (w - SMALL_W) / (BIG_W - SMALL_W));
    draw_library_card_r(ui, x, y, w, h, r, focus, alpha);
}

static void draw_row(Ui *ui)
{
    const LoaderView *v = view(ui);
    const Theme *th = theme_cur();
    float art_a = ui_art_alpha(ui);
    float launch = ui->launch < 0 ? 0 : smooth(ui->launch);
    float row_alpha = ui->intro * smooth(ui->tab_anim);
    int total = v->count > 0 ? v->count + 1 : 0; /* +1 för Spelbibliotek-kortet längst till höger */
    int first = (int)floorf(ui->pos) - 2, last = (int)floorf(ui->pos) + 8;
    if (first < 0) first = 0;
    if (last > total - 1) last = total - 1;

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
            if (p == v->count) {
                draw_library_card(ui, x, y, w, h, focus, alpha);
                continue;
            }
            if (ui->moving && p == ui->sel[ui->tab]) { /* Flytta: kortet lyfts, med pilar åt båda hållen */
                y -= 5;
                float ay = y + h / 2, pa = alpha * (0.6f + 0.4f * sinf(ui->time * 5));
                float l[6] = { x - 13, ay, x - 7, ay - 6, x - 7, ay + 6 };
                float r[6] = { x + w + 13, ay, x + w + 7, ay - 6, x + w + 7, ay + 6 };
                u32 arr = ink_over_art(th, th->ink ? th->ink->graphite : LIGHT, LIGHT, art_a);
                gfx_fan(x - 9, ay, l, 3, with_alpha(arr, pa));
                gfx_fan(x + w + 9, ay, r, 3, with_alpha(arr, pa));
            }
            draw_card(ui, v->idx[p], x, y, w, h, focus, alpha, art_a);
        }
    }
}

/* Games-skärmen: ett valt spel till vänster och fem nästa spel till höger.
 * Ikonerna och urvalet kommer från samma loader och lista som den äldre raden. */
static const char *type_name(u32 t);

/* Vertical bottom scrim whose bottom corners follow the card corner radius r cleanly. */
static void draw_card_scrim(float x, float y, float w, float h, float r, float sh, int max_alpha)
{
    if (max_alpha <= 0)
        return;
    if (r > sh)
        r = sh;
    float sy = y + h - sh, by = y + h - r;
    u32 top_c = RGBA(0, 0, 0, 0);
    u32 mid_c = RGBA(0, 0, 0, (int)(max_alpha * ((sh - r) / sh)));
    if (by > sy)
        gfx_gradient_v(x, sy, w, by - sy, top_c, mid_c);
    const int steps = 4;
    float prev_y = by, prev_dx = 0;
    u32 prev_c = mid_c;
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / steps;
        float ny = by + t * r;
        float dx = r * (1.0f - sqrtf(fmaxf(0.0f, 1.0f - t * t)));
        u32 nc = RGBA(0, 0, 0, (int)(max_alpha * ((sh - r + t * r) / sh)));
        float xy[8] = { x + prev_dx, prev_y, x + w - prev_dx, prev_y,
                        x + dx, ny, x + w - dx, ny };
        u32 cols[4] = { prev_c, prev_c, nc, nc };
        gfx_quad(xy, cols);
        prev_y = ny;
        prev_dx = dx;
        prev_c = nc;
    }
}

static float draw_meta_badge(float x, float y, const char *label, float a)
{
    const Theme *t = theme_cur();
    float bw = text_width(0.42f, 1, label) + 12.0f;
    gfx_round_rect(x, y, bw, 12.0f, 3.0f,
                   t->ink ? COLOR_ALPHA(t->ink->graphite, (int)(242 * a)) : RGBA(255, 255, 255, (int)(242 * a)));
    text_draw(x + bw / 2.0f, y + 8.5f, 0.42f, 1, with_alpha(THEME_INK(t, text_inverse, DARK), a), ALIGN_CENTER, label);
    return bw;
}

void ui_draw_footer_bar(float a)
{
    if (a <= 0.01f)
        return;
    const Theme *t = theme_cur();
    u32 bg = t->ink            ? COLOR_ALPHA(t->ink->panel, (int)(242 * a))
           : g_theme == 0      ? RGBA(11, 15, 22, (int)(226 * a))
                               : RGBA(10, 12, 18, (int)(205 * a));
    u32 line = t->ink          ? COLOR_ALPHA(t->ink->border, (int)(235 * a))
             : g_theme == 0    ? RGBA(44, 56, 74, (int)(190 * a))
                               : COLOR_ALPHA(t->accent, (int)(110 * a));
    gfx_rect(0.0f, FOOTER_Y, SCREEN_W, SCREEN_H - FOOTER_Y, bg);
    gfx_rect(0.0f, FOOTER_Y, SCREEN_W, 1.0f, line);
}

static float hints_total_width(const Ui *ui);

static void games_card_geom(float d, float *x, float *y, float *w, float *h, float *r, float *alpha_mul)
{
    const float hx = LIB_MARGIN, hy = 40.0f, hw = (float)ICON_W, hh = (float)ICON_H, hr = 7.0f;
    const float x0 = 176.0f, cy = 40.0f, cw = 90.0f, ch = 50.0f, cr = 4.5f, gap = 9.0f;
    if (d < 0.0f) {
        float t = clampf(-d, 0.0f, 1.0f);
        *w = lerpf(hw, 108.0f, t);
        *h = lerpf(hh, 60.0f, t);
        *x = lerpf(hx, hx - 92.0f, t);
        *y = hy + (hh - *h) * 0.5f;
        *r = lerpf(hr, 5.5f, t);
        *alpha_mul = clampf(1.0f + d * 1.15f, 0.0f, 1.0f);
    } else if (d < 1.0f) {
        *x = lerpf(hx, x0, d);
        *y = hy;
        *w = lerpf(hw, cw, d);
        *h = lerpf(hh, ch, d);
        *r = lerpf(hr, cr, d);
        *alpha_mul = 1.0f;
    } else {
        *x = x0 + (d - 1.0f) * (cw + gap);
        *y = cy;
        *w = cw;
        *h = ch;
        *r = cr;
        *alpha_mul = d > 3.0f ? clampf((3.75f - d) / 0.75f, 0.0f, 1.0f) : 1.0f;
    }
}

static void draw_games_card(Ui *ui, int list_index, int is_library, float x, float y, float w, float h,
                            float r, float focus, float alpha, float ring_alpha, float art_a)
{
    const Theme *th = theme_cur();
    float f = smooth(focus);
    if (is_library) {
        /* Pass focus=0 so the shared PS5 selection ring scales uniformly during card transitions. */
        draw_library_card_r(ui, x, y, w, h, r, 0, alpha);
    } else {
        gfx_glow(x, y + lerpf(2.0f, 3.0f, f), w, h, lerpf(4.0f, 8.0f, f),
                 RGBA(0, 0, 0, (int)(lerpf(85.0f, 125.0f, f) * alpha)));
        IconSlot *slot = loader_icon(list_index);
        float shown = 0.0f;
        if (slot && slot->state == SLOT_READY && slot->tex) {
            slot->shown = clampf(slot->shown + 1.0f / 9.0f, 0.0f, 1.0f);
            shown = slot->shown;
        }
        if (shown < 1.0f) {
            float pa = alpha * (1.0f - shown);
            gfx_round_rect(x, y, w, h, r, with_alpha(th->card, pa));
            const char *title = ui->list->items[list_index].title;
            char initial[2] = { (title[0] >= 'a' && title[0] <= 'z') ? title[0] - 32 : title[0], 0 };
            if ((u8)initial[0] < 0x80 && initial[0] && pa > 0.02f) {
                float s = w / (float)ICON_W;
                text_draw(x + w * 0.5f, y + h * 0.5f + 9.0f * s, 1.3f * s, 1,
                          with_alpha(THEME_INK(th, text, TEXT_MAIN), pa), ALIGN_CENTER, initial);
            }
        }
        if (shown > 0.0f) {
            int tint = (int)lerpf(238.0f, 255.0f, f);
            gfx_tex(slot->tex, x, y, w, h, RGBA(tint, tint, tint, (int)(255.0f * alpha * shown)));
        }
        int vx, vy, vw, vh;
        Texture *video = focus > 0.5f && ui->preview_anim > 0
                         ? preview_frame_sub(list_index, &vx, &vy, &vw, &vh) : NULL;
        if (video)
            gfx_tex_sub(video, (float)vx, (float)vy, (float)vw, (float)vh,
                        x, y, w, h, RGBA(255, 255, 255, (int)(255.0f * alpha * smooth(ui->preview_anim))));
        if (meta(list_index)->pinned) {
            float s = w / (float)ICON_W, px = x + w - 10.0f * s, py = y + 10.0f * s;
            gfx_circle(px, py, 6.0f * s, RGBA(18, 18, 22, (int)(200.0f * alpha)));
            gfx_circle(px, py - 1.3f * s, 2.2f * s, RGBA(240, 240, 244, (int)(255.0f * alpha)));
            gfx_rect(px - 0.5f * s, py - 0.5f * s, 1.0f * s, 3.8f * s, RGBA(240, 240, 244, (int)(255.0f * alpha)));
        }
    }

    if (is_library && f < 0.99f) {
        float la = alpha * (1.0f - f);
        draw_card_scrim(x, y, w, h, r, 17.0f, (int)(205.0f * la));
        text_draw(x + w * 0.5f, y + h - 5.0f, 0.38f, 1,
                  with_alpha(THEME_INK(th, text_inverse, TEXT_MAIN), la), ALIGN_CENTER, L(S_TAB_LIBRARY));
    }

    if (f > 0.01f && ring_alpha > 0.01f) {
        u32 ring = ink_over_art(th, COLOR_ALPHA(th->ink ? th->ink->sel_edge : LIGHT, (int)(242.0f * f * ring_alpha)),
                                RGBA(255, 255, 255, (int)(242.0f * f * ring_alpha)), art_a);
        gfx_ring(x - 3.0f, y - 3.0f, w + 6.0f, h + 6.0f, r + 3.0f, ring);
    }
}

static void draw_games_redesign(Ui *ui)
{
    const LoaderView *v = view(ui);
    const Theme *th = theme_cur();
    float art_a = ui_art_alpha(ui);
    float a = ui->intro * smooth(ui->tab_anim);
    float launch = ui->launch < 0 ? 0 : smooth(ui->launch);
    float ui_a = a * (1.0f - smooth(launch * 2.0f));
    if (!v->count)
        return;

    int sel = ui->sel[ui->tab];
    if (sel < 0) sel = 0;
    if (sel > v->count) sel = v->count;
    if (ui->cat[ui->tab] > 0) {
        const char *ct = cat_title(ui->cat[ui->tab]);
        float cw = text_width(0.46f, 0, ct) + 14;
        u32 cpill = th->ink      ? COLOR_ALPHA(th->ink->graphite, (int)(220 * ui_a))
                  : g_theme == 0 ? RGBA(37, 84, 150, (int)(200 * ui_a))
                                 : COLOR_ALPHA(theme_toggle_on(th), (int)(200 * ui_a));
        gfx_round_rect(LIB_MARGIN, 148, cw, 16, 8, cpill);
        text_draw(LIB_MARGIN + 7, 159.5f, 0.46f, 0,
                  with_alpha(THEME_INK(th, text_inverse, TEXT_MAIN), ui_a), ALIGN_LEFT, ct);
    }

    int total = v->count + 1; /* +1 för Spelbibliotek-kortet längst till höger */
    int first = (int)floorf(ui->pos) - 1, last = (int)floorf(ui->pos) + 4;
    if (first < 0) first = 0;
    if (last > total - 1) last = total - 1;
    int nearest = ui->moving || launch > 0.0f ? sel : (int)floorf(ui->pos + 0.5f);

    for (int pass = 0; pass < 2; pass++) {
        for (int p = last; p >= first; p--) {
            if ((p == nearest) != (pass == 1))
                continue;
            float d = (ui->moving && p == sel) ? 0.0f : (float)p - ui->pos;
            float x, y, w, h, r, amul;
            games_card_geom(d, &x, &y, &w, &h, &r, &amul);
            if (ui->moving && p == sel)
                y -= 3.5f;
            float focus = clampf(1.0f - fabsf(d), 0.0f, 1.0f);
            float alpha = a * amul;
            float ring_a = alpha;
            if (launch > 0.0f) {
                if (p == sel) {
                    float grow = 1.0f + launch * 1.4f;
                    float cx = lerpf(x + w * 0.5f, SCREEN_W * 0.5f, launch);
                    float cy = lerpf(y + h * 0.5f, SCREEN_H * 0.5f, launch);
                    w *= grow;
                    h *= grow;
                    x = cx - w * 0.5f;
                    y = cy - h * 0.5f;
                    r *= grow;
                    alpha = ui->intro * (1.0f - smooth(fmaxf(0.0f, launch - 0.15f) * 1.3f));
                    ring_a = alpha * (1.0f - smooth(launch * 2.5f));
                } else {
                    alpha *= 1.0f - smooth(launch * 1.5f);
                    ring_a = 0.0f;
                }
            }
            if ((p != sel || launch <= 0.0f) && (x > SCREEN_W || x + w < -8.0f || alpha <= 0.01f))
                continue;
            int lib_card = p == v->count;
            draw_games_card(ui, lib_card ? -1 : v->idx[p], lib_card, x, y, w, h, r, focus, alpha, ring_a, art_a);
        }
    }

    int is_library = sel == v->count;
    int selected = is_library ? -1 : v->idx[sel];
    float la = smooth(ui->label_anim);
    char name[96];
    float name_w = 380.0f;
    text_ellipsis(name, sizeof(name), is_library ? L(S_TAB_LIBRARY) : ui->list->items[selected].title, 0.66f, name_w);
    u32 title_col = ink_over_art(th, th->ink ? th->ink->text : TEXT_MAIN, TEXT_MAIN, art_a);
    text_draw(LIB_MARGIN + (1.0f - la) * 14.0f, 138.0f, text_fit(0.66f, 0.48f, 1, name_w, name), 1,
              with_alpha(title_col, ui_a * la), ALIGN_LEFT, name);

    int confirm = ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS;
    float fy = 257.0f;
    u32 foot_dim = th->ink ? COLOR_ALPHA(th->ink->text_dim, (int)(240 * ui_a)) : RGBA(203, 213, 225, (int)(225 * ui_a));
    if (is_library) {
        char count[24];
        snprintf(count, sizeof(count), "%d", ui->views[TAB_LIBRARY].count);
        float bw = draw_meta_badge(LIB_MARGIN, 251.0f, "LIB", ui_a);
        text_draw(LIB_MARGIN + bw + 8.0f, 260.5f, 0.54f, 0, foot_dim, ALIGN_LEFT, count);
        return;
    }

    const Entry *e = &ui->list->items[selected];
    if (!ui->moving) {
        float lx = LIB_MARGIN;
        float bw = draw_meta_badge(lx, 251.0f, type_name(e->type), ui_a);
        lx += bw + 8.0f;
        if (e->type != ENTRY_HOMEBREW && e->game_id[0]) {
            text_draw(lx, 260.5f, 0.54f, 0, foot_dim, ALIGN_LEFT, e->game_id);
            lx += text_width(0.54f, 0, e->game_id) + 14.0f;
        }
        {
            float oa = ui_a * (1.0f - smooth(ui->menu_anim));
            float max_opt = SCREEN_W - LIB_MARGIN - hints_total_width(ui) - 14.0f - (lx + 17.0f);
            if (oa > 0.02f && max_opt > 24.0f) {
                float osz = text_fit(0.56f, 0.40f, 0, max_opt, L(S_OPTIONS));
                theme_button(th, lx + 6.5f, fy, 6.5f, BTN_SQUARE, 0,
                             with_alpha(LIGHT, 0.96f * oa), with_alpha(DARK, oa));
                u32 opt_col = th->ink ? COLOR_ALPHA(th->ink->text, (int)(245 * oa))
                                      : RGBA(218, 224, 235, (int)(238 * oa));
                text_draw(lx + 17.0f, fy + 4.5f, osz, 0, opt_col, ALIGN_LEFT, L(S_OPTIONS));
            }
        }
    } else {
        float hx = LIB_MARGIN, hy = 40.0f - 3.5f, hw = (float)ICON_W, hh = (float)ICON_H;
        float ay = hy + hh / 2.0f, pa = ui_a * (0.6f + 0.4f * sinf(ui->time * 5.0f));
        float ltri[6] = { hx - 12.0f, ay, hx - 6.0f, ay - 6.0f, hx - 6.0f, ay + 6.0f };
        float rtri[6] = { hx + hw + 12.0f, ay, hx + hw + 6.0f, ay - 6.0f, hx + hw + 6.0f, ay + 6.0f };
        u32 card_arr = ink_over_art(th, th->ink ? th->ink->graphite : LIGHT, LIGHT, art_a);
        gfx_fan(hx - 8.0f, ay, ltri, 3, with_alpha(card_arr, pa));
        gfx_fan(hx + hw + 8.0f, ay, rtri, 3, with_alpha(card_arr, pa));

        /* Flytta-läge: lägg kontrollerna i fotlisten precis som i biblioteket */
        float hx_f = LIB_MARGIN;
        u32 foot_arr = THEME_INK(th, graphite, LIGHT);
        u32 foot_tx = th->ink ? COLOR_ALPHA(th->ink->text, (int)(245 * ui_a)) : RGBA(218, 224, 235, (int)(238 * ui_a));
        float l[6] = { hx_f, fy, hx_f + 6.0f, fy - 4.5f, hx_f + 6.0f, fy + 4.5f };
        gfx_fan(hx_f + 3.0f, fy, l, 3, with_alpha(foot_arr, 0.94f * ui_a));
        float r[6] = { hx_f + 15.0f, fy, hx_f + 9.0f, fy - 4.5f, hx_f + 9.0f, fy + 4.5f };
        gfx_fan(hx_f + 12.0f, fy, r, 3, with_alpha(foot_arr, 0.94f * ui_a));
        hx_f += 20.0f;
        text_draw(hx_f, fy + 4.5f, 0.56f, 0, foot_tx, ALIGN_LEFT, L(S_F_MOVE));
        hx_f += text_width(0.56f, 0, L(S_F_MOVE)) + 14.0f;
        theme_button(th, hx_f + 6.5f, fy, 6.5f, confirm, 0, with_alpha(LIGHT, 0.96f * ui_a), with_alpha(DARK, ui_a));
        text_draw(hx_f + 17.0f, fy + 4.5f, 0.56f, 0, foot_tx, ALIGN_LEFT, L(S_F_DONE));
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
    const Theme *th = theme_cur();
    float art_a = ui_art_alpha(ui);
    float fade = ui->intro * smooth(ui->tab_anim) * (ui->launch < 0 ? 1 : 1 - smooth(ui->launch * 2));
    if (fade <= 0)
        return;

    u32 title_col = ink_over_art(th, th->ink ? th->ink->text : TEXT_MAIN, TEXT_MAIN, art_a);
    u32 dim_col = ink_over_art(th, th->ink ? th->ink->text_dim : TEXT_DIM, TEXT_DIM, art_a);

    /* Spelbibliotek-kortet längst till höger i Spel-raden */
    if (ui->tab == TAB_GAMES && view(ui)->count > 0 && ui->sel[TAB_GAMES] == view(ui)->count) {
        float la = smooth(ui->label_anim);
        text_draw(SMALL_X + (1 - la) * 8, LABEL_Y, ui->label_size, 0, with_alpha(title_col, la * fade),
                  ALIGN_LEFT, ui->label);
        const char *badge = "LIB";
        float bw = text_width(0.5f, 1, badge) + 12, y = 206;
        gfx_round_rect(MARGIN, y, bw, 15, 3, with_alpha(THEME_INK(th, graphite, LIGHT), 0.92f * fade));
        text_draw(MARGIN + bw / 2, y + 11, 0.5f, 1, with_alpha(THEME_INK(th, text_inverse, DARK), fade), ALIGN_CENTER, badge);
        char cnt[32];
        snprintf(cnt, sizeof(cnt), "%d", ui->views[TAB_LIBRARY].count);
        text_draw(MARGIN + bw + 8, y + 11.5f, 0.62f, 0, with_alpha(dim_col, fade), ALIGN_LEFT, cnt);

        const char *open = L(S_F_OPEN);
        float bx = MARGIN, by = 233, bh = 26;
        float tw = text_width(0.66f, 0, open);
        float w = 8 + 16 + 8 + tw + 14;
        gfx_glow(bx, by + 2, w, bh, 6, RGBA(0, 0, 0, (int)(110 * fade)));
        gfx_round_rect(bx, by, w, bh, bh / 2, with_alpha(THEME_INK(th, graphite, LIGHT), 0.95f * fade));
        theme_button(th, bx + 16, by + bh / 2, 8, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, 1,
                     with_alpha(DARK, fade), with_alpha(LIGHT, fade));
        text_draw(bx + 32, by + 17.5f, 0.66f, 0, with_alpha(THEME_INK(th, text_inverse, DARK), fade), ALIGN_LEFT, open);
        return;
    }

    int i = ui_selected(ui);
    if (i < 0)
        return;
    const Entry *e = &ui->list->items[i];

    /* spelets logga (PIC0) till vänster under kortet, som titelloggan på PS5 – nederkanten
     * står fast ovanför typetiketten */
    if (ui->logo_cur && ui->bg_index == i) {
        Texture *lg = ui->logo_cur;
        float la = fade * smooth(ui->bg_fade) * (1 - smooth(ui->menu_anim));
        gfx_tex(lg, MARGIN, 198 - lg->h, lg->w, lg->h, RGBA(255, 255, 255, (int)(255 * la)));
    }

    /* titeln bredvid kortet glider in när markeringen byts */
    float la = smooth(ui->label_anim);
    text_draw(SMALL_X + (1 - la) * 8, LABEL_Y, ui->label_size, 0, with_alpha(title_col, la * fade),
              ALIGN_LEFT, ui->label);

    /* nere till vänster: typetikett, spel-ID och startknapp (i linje med knapptipsen på y = 246) */
    const char *type = type_name(e->type);
    float bw = text_width(0.5f, 1, type) + 12;
    float y = 206;
    gfx_round_rect(MARGIN, y, bw, 15, 3, with_alpha(THEME_INK(th, graphite, LIGHT), 0.92f * fade));
    text_draw(MARGIN + bw / 2, y + 11, 0.5f, 1, with_alpha(THEME_INK(th, text_inverse, DARK), fade), ALIGN_CENTER, type);
    if (e->type != ENTRY_HOMEBREW && e->game_id[0])
        text_draw(MARGIN + bw + 8, y + 11.5f, 0.62f, 0, with_alpha(dim_col, fade), ALIGN_LEFT, e->game_id);

    if (ui->moving) { /* Flytta: ◀ ▶ Flytta   ✕ Klart i stället för Starta */
        float hx = MARGIN, hy = 246;
        float l[6] = { hx, hy, hx + 6, hy - 4.5f, hx + 6, hy + 4.5f };
        float r[6] = { hx + 15, hy, hx + 9, hy - 4.5f, hx + 9, hy + 4.5f };
        u32 arr = ink_over_art(th, th->ink ? th->ink->graphite : LIGHT, LIGHT, art_a);
        gfx_fan(hx + 3, hy, l, 3, with_alpha(arr, 0.9f * fade));
        gfx_fan(hx + 12, hy, r, 3, with_alpha(arr, 0.9f * fade));
        hx += 21;
        text_draw(hx, hy + 4.5f, 0.6f, 0, with_alpha(title_col, fade), ALIGN_LEFT, L(S_F_MOVE));
        hx += text_width(0.6f, 0, L(S_F_MOVE)) + 14;
        theme_button(th, hx + 6.5f, hy, 6.5f, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, art_a > 0.3f,
                     with_alpha(LIGHT, 0.9f * fade), with_alpha(DARK, fade));
        text_draw(hx + 17, hy + 4.5f, 0.6f, 0, with_alpha(title_col, fade), ALIGN_LEFT, L(S_F_DONE));
        return;
    }
    const char *go = L(S_LAUNCH);
    float bx = MARGIN, by = 233, bh = 26;
    float tw = text_width(0.66f, 0, go);
    float w = 8 + 16 + 8 + tw + 14;
    gfx_glow(bx, by + 2, w, bh, 6, RGBA(0, 0, 0, (int)(110 * fade)));
    gfx_round_rect(bx, by, w, bh, bh / 2, with_alpha(THEME_INK(th, graphite, LIGHT), 0.95f * fade));
    theme_button(th, bx + 8 + 8, by + bh / 2, 8, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, 1,
                 with_alpha(DARK, fade), with_alpha(LIGHT, fade));
    text_draw(bx + 32, by + 17.5f, 0.66f, 0, with_alpha(THEME_INK(th, text_inverse, DARK), fade), ALIGN_LEFT, go);

    /* □ Alternativ (inte för UMD-skivan) – på samma y = 246 som L R-tipsen till höger */
    float oa = fade * (1 - smooth(ui->menu_anim));
    float mx = bx + w + 16, my = by + bh / 2;
    float ow = 13 + 6 + text_width(0.6f, 0, L(S_OPTIONS));
    gfx_round_rect(mx - 9, my - 11, ow + 18, 22, 11,
                   th->ink && art_a <= 0.3f ? COLOR_ALPHA(th->ink->panel, (int)(225 * oa)) : RGBA(0, 0, 0, (int)(120 * oa)));
    theme_button(th, mx + 6.5f, my, 6.5f, BTN_SQUARE, art_a > 0.3f, with_alpha(LIGHT, 0.9f * oa), with_alpha(DARK, oa));
    text_draw(mx + 19, my + 4.5f, 0.6f, 0, with_alpha(title_col, oa), ALIGN_LEFT, L(S_OPTIONS));
}

/* Media-raden: tre PS5-kort för Musik, Bilder och Videor */
static void draw_media_card(int kind, float x, float y, float w, float h, float focus, float alpha)
{
    const Theme *th = theme_cur();
    float r = lerpf(5.0f, BIG_R, (w - SMALL_W) / (BIG_W - SMALL_W));
    float s = w / BIG_W;
    gfx_glow(x + 1, y + 3, w - 2, h - 2, 5 + 4 * focus, RGBA(0, 0, 0, (int)(130 * alpha)));
    u32 cc = th->card;
    u32 bg = th->ink ? COLOR_ALPHA(th->ink->panel, (int)(242 * alpha))
                     : RGBA((cc & 0xFF) + 6, ((cc >> 8) & 0xFF) + 7, ((cc >> 16) & 0xFF) + 12, (int)(238 * alpha));
    gfx_round_rect(x, y, w, h, r, bg);
    float cx = x + w / 2, cy = y + h / 2;
    u32 ic = th->ink ? COLOR_ALPHA(th->ink->graphite, (int)((185 + 65 * focus) * alpha))
                     : RGBA(242, 244, 250, (int)((185 + 65 * focus) * alpha));
    u32 hole = bg;
    if (kind == 0) { /* Musik: not */
        draw_note(cx, cy + 2.0f * s, 32.0f * s, ic);
    } else if (kind == 1) { /* Bilder: ram med sol och berg */
        gfx_round_rect(cx - 15.0f * s, cy - 11.5f * s, 30.0f * s, 23.0f * s, 4.0f * s, ic);
        gfx_round_rect(cx - 12.2f * s, cy - 8.7f * s, 24.4f * s, 17.4f * s, 2.2f * s, hole);
        gfx_circle(cx - 5.5f * s, cy - 2.8f * s, 2.6f * s, ic);
        float m[6] = { cx - 7.5f * s, cy + 7.0f * s, cx + 2.2f * s, cy - 2.2f * s, cx + 10.5f * s, cy + 7.0f * s };
        gfx_fan(cx + 2.0f * s, cy + 4.0f * s, m, 3, ic);
    } else { /* Videor: filmruta med play-triangel */
        gfx_round_rect(cx - 15.5f * s, cy - 11.5f * s, 31.0f * s, 23.0f * s, 4.0f * s, ic);
        gfx_round_rect(cx - 12.5f * s, cy - 8.5f * s, 25.0f * s, 17.0f * s, 2.2f * s, hole);
        float tri[6] = { cx - 3.6f * s, cy - 5.2f * s, cx + 5.6f * s, cy, cx - 3.6f * s, cy + 5.2f * s };
        gfx_fan(cx, cy, tri, 3, ic);
    }
    if (focus > 0.01f)
        gfx_ring(x - 4, y - 4, w + 8, h + 8, r + 4,
                 COLOR_ALPHA(THEME_INK(th, sel_edge, RGBA(240, 240, 244, 255)), (int)(235 * focus * alpha)));
}

static void draw_media_row(Ui *ui)
{
    const Theme *th = theme_cur();
    float art_a = ui_art_alpha(ui);
    float row_alpha = ui->intro * smooth(ui->tab_anim);
    int nearest = (int)floorf(ui->pos + 0.5f);
    for (int pass = 0; pass < 2; pass++) {
        for (int p = 2; p >= 0; p--) {
            if ((p == nearest) != (pass == 1))
                continue;
            float d = p - ui->pos;
            float x, y, w, h;
            card_geometry(d, &x, &w, &h, &y);
            float focus = 1 - clampf(fabsf(d), 0, 1);
            float alpha = row_alpha * (d < -0.5f ? clampf(1.5f + d, 0, 1) : 1);
            draw_media_card(p, x, y, w, h, focus, alpha);
        }
    }

    u32 title_col = ink_over_art(th, th->ink ? th->ink->text : TEXT_MAIN, TEXT_MAIN, art_a);
    u32 dim_col = ink_over_art(th, th->ink ? th->ink->text_dim : TEXT_DIM, TEXT_DIM, art_a);
    float la = smooth(ui->label_anim);
    text_draw(SMALL_X + (1 - la) * 8, LABEL_Y, ui->label_size, 0, with_alpha(title_col, la * row_alpha),
              ALIGN_LEFT, ui->label);

    static const char *const badges[3] = { "MP3", "IMG", "VIDEO" };
    int counts[3] = { music_count(), s_npics, s_nvids };
    int sel = clampf(ui->sel[TAB_MEDIA], 0, 2);
    const char *badge = badges[sel];
    float bw = text_width(0.5f, 1, badge) + 12, y = 206;
    gfx_round_rect(MARGIN, y, bw, 15, 3, with_alpha(THEME_INK(th, graphite, LIGHT), 0.92f * row_alpha));
    text_draw(MARGIN + bw / 2, y + 11, 0.5f, 1, with_alpha(THEME_INK(th, text_inverse, DARK), row_alpha), ALIGN_CENTER, badge);
    char cnt[32];
    if (sel > 0 && media_loading())
        snprintf(cnt, sizeof(cnt), "...");
    else
        snprintf(cnt, sizeof(cnt), "%d", counts[sel]);
    text_draw(MARGIN + bw + 8, y + 11.5f, 0.62f, 0, with_alpha(dim_col, row_alpha), ALIGN_LEFT, cnt);

    const char *open = L(S_F_OPEN);
    float bx = MARGIN, by = 233, bh = 26;
    float tw = text_width(0.66f, 0, open);
    float w = 8 + 16 + 8 + tw + 14;
    gfx_glow(bx, by + 2, w, bh, 6, RGBA(0, 0, 0, (int)(110 * row_alpha)));
    gfx_round_rect(bx, by, w, bh, bh / 2, with_alpha(THEME_INK(th, graphite, LIGHT), 0.95f * row_alpha));
    theme_button(th, bx + 16, by + bh / 2, 8, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, 1,
                 with_alpha(DARK, row_alpha), with_alpha(LIGHT, row_alpha));
    text_draw(bx + 32, by + 17.5f, 0.66f, 0, with_alpha(THEME_INK(th, text_inverse, DARK), row_alpha), ALIGN_LEFT, open);
}

/* Alternativmenyn (□): en panel nere till vänster, över Starta-knappen, med kortets namn,
 * fäst, flytta, sortering och ✕ / ○ längst ner. */
static void draw_menu(Ui *ui)
{
    const Theme *th = theme_cur();
    float a = smooth(ui->menu_anim);
    int i = ui_selected(ui);
    if (a <= 0 || i < 0)
        return;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(110 * a)));
    const float row = 26;
    float w = 214, h = 26 + ui->menu_rows * row + 34, x = MARGIN - 6, y = 258 - h + (1 - a) * 10;
    gfx_glow(x, y, w, h, 10, RGBA(0, 0, 0, (int)(150 * a)));
    if (th->ink) {
        gfx_round_rect(x, y, w, h, 9, COLOR_ALPHA(th->ink->border, (int)(255 * a)));
        gfx_round_rect(x + 1, y + 1, w - 2, h - 2, 8.2f, COLOR_ALPHA(th->ink->graphite, (int)(255 * a)));
    } else {
        gfx_round_rect(x, y, w, h, 9, RGBA(40, 41, 47, (int)(255 * a)));
    }

    char title[160];
    text_ellipsis(title, sizeof(title), ui->list->items[i].title, 0.5f, w - 24);
    u32 hdr = th->ink ? COLOR_ALPHA(th->ink->panel_alt, (int)(230 * a)) : with_alpha(TEXT_DIM, a);
    text_draw(x + 12, y + 18, 0.5f, 0, hdr, ALIGN_LEFT, title);

    u32 inv = THEME_INK(th, text_inverse, TEXT_MAIN);
    u32 sel_fg = THEME_INK(th, sel_ink, DARK);
    for (int k = 0; k < ui->menu_rows; k++) {
        char buf[64];
        const char *item = menu_label(ui, k, buf, sizeof(buf));
        float ry = y + 26 + k * row;
        int sel = k == ui->menu_sel;
        if (sel)
            theme_sel_box(th, x + 6, ry, w - 12, row - 2, 7, with_alpha(LIGHT, 0.95f * a));
        text_draw(x + 16, ry + 17, text_fit(0.6f, 0.45f, 0, w - 32, item), 0,
                  with_alpha(sel ? sel_fg : inv, a), ALIGN_LEFT, item);
    }

    float fx = x + 12, fy = y + h - 18;
    theme_button(th, fx + 6.5f, fy, 6.5f, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, 1,
                 with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    fx += 17;
    text_draw(fx, fy + 4.5f, 0.52f, 0, with_alpha(inv, a), ALIGN_LEFT, L(S_SELECT));
    fx += text_width(0.52f, 0, L(S_SELECT)) + 12;
    theme_button(th, fx + 6.5f, fy, 6.5f, ui->confirm_circle ? BTN_CROSS : BTN_CIRCLE, 1,
                 with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    fx += 17;
    text_draw(fx, fy + 4.5f, 0.52f, 0, with_alpha(inv, a), ALIGN_LEFT, L(S_BACK));
}

static int is_shoulder_key(const char *key)
{
    return key && (key[0] == 'L' || key[0] == 'R') && key[1] == '\0';
}

static float key_cap_width(const char *key)
{
    if (is_shoulder_key(key))
        return (float)ICON_SHLD_W;
    float w = text_width(0.42f, 1, key) + 8.0f;
    return w < 14.0f ? 14.0f : w;
}

static float key_cap(float x, float y, const char *key, float a)
{
    const Theme *t = theme_cur();
    float kw = key_cap_width(key);
    if (is_shoulder_key(key)) {
        static Texture *plate_tex, *l_tex, *r_tex;
        float py = y - ICON_SHLD_H * 0.5f;
        const unsigned char *glyph = key[0] == 'L' ? ICON_SHLD_L : ICON_SHLD_R;
        Texture **gtex = key[0] == 'L' ? &l_tex : &r_tex;
        if (t->ink) {
            draw_icon_wh(ICON_SHLD_PLATE, ICON_SHLD_W, ICON_SHLD_H, &plate_tex, x, py + 1.0f,
                         with_alpha(t->ink->shadow, 0.92f * a));
            draw_icon_wh(ICON_SHLD_PLATE, ICON_SHLD_W, ICON_SHLD_H, &plate_tex, x, py,
                         with_alpha(t->ink->panel_hi, 0.96f * a));
            draw_icon_wh(glyph, ICON_SHLD_W, ICON_SHLD_H, gtex, x, py, with_alpha(t->ink->graphite, a));
        } else {
            draw_icon_wh(ICON_SHLD_PLATE, ICON_SHLD_W, ICON_SHLD_H, &plate_tex, x, py,
                         with_alpha(LIGHT, 0.94f * a));
            draw_icon_wh(glyph, ICON_SHLD_W, ICON_SHLD_H, gtex, x, py, with_alpha(DARK, a));
        }
        return kw;
    }
    if (t->ink) {
        gfx_round_rect(x, y - 7.0f, kw, 14.0f, 3.0f, with_alpha(t->ink->shadow, 0.92f * a));
        gfx_round_rect(x + 1.0f, y - 6.0f, kw - 2.0f, 12.0f, 2.2f, with_alpha(t->ink->panel_hi, 0.96f * a));
        text_draw(x + kw / 2.0f, y + 3.5f, 0.42f, 1, with_alpha(t->ink->graphite, a), ALIGN_CENTER, key);
    } else {
        gfx_round_rect(x, y - 7.0f, kw, 14.0f, 3.0f, with_alpha(LIGHT, 0.94f * a));
        text_draw(x + kw / 2.0f, y + 3.5f, 0.42f, 1, with_alpha(DARK, a), ALIGN_CENTER, key);
    }
    return kw;
}

static float hints_width_for(float tab_size)
{
    float lw = key_cap_width("L"), rw = key_cap_width("R");
    return lw + 2.0f + rw + 6.0f + text_width(tab_size, 0, L(S_HINT_TAB));
}

static float hints_width(void);

static const char *hints_confirm_label(const Ui *ui)
{
    int cnt = view(ui)->count;
    if (ui->tab == TAB_LIBRARY && cnt > 0)
        return L(S_F_OPEN);
    if ((ui->tab == TAB_GAMES || ui->tab == TAB_APPS) && cnt > 0 && !ui->moving)
        return ui->sel[ui->tab] == cnt ? L(S_F_OPEN) : L(S_LAUNCH);
    return NULL;
}

static float hints_total_width(const Ui *ui)
{
    int figma = ui->tab == TAB_GAMES || ui->tab == TAB_APPS || ui->tab == TAB_LIBRARY || ui->tab == TAB_MUSIC;
    int show_settings = figma && ui->tab != TAB_MUSIC && !(ui->moving && (ui->tab == TAB_GAMES || ui->tab == TAB_APPS));
    const char *conf = hints_confirm_label(ui);
    float base = figma ? hints_width_for(0.56f) : hints_width();
    float sw = show_settings ? 29.0f + text_width(0.56f, 0, L(S_SETTINGS)) : 0.0f;
    float cw = conf ? 29.0f + text_width(0.56f, 0, conf) : 0.0f;
    return base + sw + cw;
}

static void draw_hints(Ui *ui)
{
    const Theme *t = theme_cur();
    float art_a = ui_art_alpha(ui);
    float a = ui->intro * (ui->launch < 0 ? 1 : 1 - smooth(ui->launch * 2));
    const char *tab = L(S_HINT_TAB);
    int figma = ui->tab == TAB_GAMES || ui->tab == TAB_APPS || ui->tab == TAB_LIBRARY || ui->tab == TAB_MUSIC;
    int show_settings = figma && ui->tab != TAB_MUSIC && !(ui->moving && (ui->tab == TAB_GAMES || ui->tab == TAB_APPS));
    const char *conf = hints_confirm_label(ui);
    float tab_sz = figma ? 0.56f : 0.60f;
    float w = hints_total_width(ui);
    float x = SCREEN_W - (figma ? LIB_MARGIN : MARGIN) - w, y = figma ? 257.0f : 246.0f;

    int on_dark = !figma && (!t->ink || art_a > 0.3f);
    if (!figma) {
        if (t->ink && art_a <= 0.3f) {
            gfx_round_rect(x - 9, y - 11, w + 18, 22, 11, COLOR_ALPHA(t->ink->border, (int)(230 * a)));
            gfx_round_rect(x - 8, y - 10, w + 16, 20, 10, COLOR_ALPHA(t->ink->panel, (int)(235 * a)));
        } else {
            gfx_round_rect(x - 9, y - 11, w + 18, 22, 11, RGBA(0, 0, 0, (int)(120 * a)));
        }
    }
    u32 label_col = figma ? (t->ink ? COLOR_ALPHA(t->ink->text, (int)(245 * a)) : RGBA(218, 224, 235, (int)(238 * a)))
                          : with_alpha(ink_over_art(t, t->ink ? t->ink->text : TEXT_MAIN, TEXT_MAIN, art_a), a);
    x += key_cap(x, y, "L", a) + 2.0f;
    x += key_cap(x, y, "R", a) + 6.0f;
    text_draw(x, y + 4.5f, tab_sz, 0, label_col, ALIGN_LEFT, tab);
    if (show_settings) {
        x += text_width(tab_sz, 0, tab) + 12.0f;
        theme_button(t, x + 6.5f, y, 6.5f, BTN_TRIANGLE, on_dark,
                     with_alpha(LIGHT, 0.96f * a), with_alpha(DARK, a));
        text_draw(x + 17.0f, y + 4.5f, 0.56f, 0, label_col, ALIGN_LEFT, L(S_SETTINGS));
        x += 17.0f + text_width(0.56f, 0, L(S_SETTINGS));
    } else if (conf) {
        x += text_width(tab_sz, 0, tab);
    }
    if (conf) {
        x += 12.0f;
        theme_button(t, x + 6.5f, y, 6.5f, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, on_dark,
                     with_alpha(LIGHT, 0.96f * a), with_alpha(DARK, a));
        text_draw(x + 17.0f, y + 4.5f, 0.56f, 0, label_col, ALIGN_LEFT, conf);
    }
}

static float hints_width(void)
{
    static float w;
    static const char *for_lang; /* bredden ändras bara med språket */
    if (for_lang != L(S_HINT_TAB)) {
        w = hints_width_for(0.60f);
        for_lang = L(S_HINT_TAB);
    }
    return w;
}

static void draw_library_tile(Ui *ui, int list_index, float x, float y, float w, float h,
                              float focus, float card_a, float title_a, float art_a)
{
    const Theme *t = theme_cur();
    draw_card(ui, list_index, x, y, w, h, focus, card_a, art_a);
    IconSlot *slot = loader_icon(list_index);
    float shown = slot && slot->state == SLOT_READY && slot->tex ? slot->shown : 0.0f;
    if (shown <= 0.02f || title_a <= 0.02f)
        return;

    float s = w / CELL_W;
    float r = 4.5f * s;
    float ta = title_a * shown;
    draw_card_scrim(x, y, w, h, r, 16.0f * s, (int)(205 * ta));
    char label[48];
    float tsz = 0.36f * s;
    text_ellipsis(label, sizeof(label), ui->list->items[list_index].title, tsz, w - 10.0f * s);
    text_draw(x + 5.5f * s, y + h - 5.5f * s, tsz, 1,
              with_alpha(THEME_INK(t, text_inverse, TEXT_MAIN), ta), ALIGN_LEFT, label);
}

/* Biblioteket: allt i ett rutnät, rubrik med antal och sortering, vald titel nere till vänster. */
static void draw_library(Ui *ui)
{
    const LoaderView *v = view(ui);
    const Theme *t = theme_cur();
    float art_a = ui_art_alpha(ui);
    float launch = ui->launch < 0 ? 0 : smooth(ui->launch);
    float a = ui->intro * smooth(ui->tab_anim);
    float ui_a = a * (1 - smooth(launch * 2));
    float slide = (1 - smooth(ui->tab_anim)) * 10;

    /* Rensa sorteringskapsel uppe till höger för att lämna plats till kategori-knappen till vänster. */
    char sort_txt[64];
    snprintf(sort_txt, sizeof(sort_txt), "%s: %s", L(S_SORT), L(ui->lib_sort ? S_SORT_RECENT : S_SORT_NAME));
    float sw_txt = text_width(0.50f, 0, sort_txt);
    float sort_w = 10.0f + 9.0f + 6.0f + sw_txt + 10.0f;
    float sort_x = SCREEN_W - LIB_MARGIN - sort_w, sort_y = 35.5f;
    u32 sort_border = t->ink       ? COLOR_ALPHA(t->ink->border, (int)(235 * ui_a))
                    : g_theme == 0 ? RGBA(62, 74, 94, (int)(200 * ui_a))
                                   : COLOR_ALPHA(t->accent, (int)(135 * ui_a));
    u32 sort_fill = t->ink         ? COLOR_ALPHA(t->ink->panel, (int)(242 * ui_a))
                  : g_theme == 0   ? RGBA(23, 30, 42, (int)(240 * ui_a))
                                   : COLOR_ALPHA(t->card, (int)(235 * ui_a));
    u32 sort_fg = t->ink ? COLOR_ALPHA(t->ink->text, (int)(245 * ui_a)) : RGBA(203, 213, 225, (int)(242 * ui_a));
    gfx_round_rect(sort_x, sort_y, sort_w, 20.0f, 10.0f, sort_border);
    gfx_round_rect(sort_x + 1.0f, sort_y + 1.0f, sort_w - 2.0f, 18.0f, 9.0f, sort_fill);
    gfx_frame(sort_x + 10.0f, sort_y + 5.5f, 9.0f, 9.0f, 1.2f, sort_fg);
    text_draw(sort_x + 25.0f, sort_y + 13.5f, 0.50f, 0, sort_fg, ALIGN_LEFT, sort_txt);

    /* Rubrik + antal-kapsel till vänster. */
    char head[96];
    const char *raw_head = ui->cat[TAB_LIBRARY] ? cat_title(ui->cat[TAB_LIBRARY]) : L(S_LIB_ALL);
    float max_head_w = sort_x - LIB_MARGIN - 52.0f;
    text_ellipsis(head, sizeof(head), raw_head, 0.66f, max_head_w > 60.0f ? max_head_w : 60.0f);
    float hw = text_width(0.66f, 1, head);
    u32 head_col = ink_over_art(t, t->ink ? t->ink->text : TEXT_MAIN, TEXT_MAIN, art_a);
    text_draw(LIB_MARGIN, 49.0f, 0.66f, 1, with_alpha(head_col, ui_a), ALIGN_LEFT, head);
    char num[16];
    snprintf(num, sizeof(num), "%d", v->count);
    float nw = text_width(0.48f, 1, num);
    float pw = nw + 14.0f;
    if (pw < 20.0f)
        pw = 20.0f;
    float cx = LIB_MARGIN + hw + 8.0f;
    u32 count_bg = t->ink       ? COLOR_ALPHA(t->ink->panel, (int)(240 * ui_a))
                 : g_theme == 0 ? RGBA(34, 45, 64, (int)(225 * ui_a))
                                : COLOR_ALPHA(t->card, (int)(235 * ui_a));
    u32 count_fg = t->ink ? COLOR_ALPHA(t->ink->text, (int)(245 * ui_a)) : RGBA(168, 182, 204, (int)(245 * ui_a));
    gfx_round_rect(cx, 37.5f, pw, 16.0f, 8.0f, count_bg);
    text_draw(cx + pw / 2.0f, 48.5f, 0.48f, 1, count_fg, ALIGN_CENTER, num);

    if (s_ncat) {
        float kx = cx + pw + 10.0f;
        float cap_w = key_cap_width("SELECT") + 5.0f;
        float avail = sort_x - 10.0f - (kx + cap_w);
        if (avail > 28.0f) {
            kx += key_cap(kx, 45.5f, "SELECT", ui_a) + 5.0f;
            char cat_lbl[40];
            text_ellipsis(cat_lbl, sizeof(cat_lbl), L(S_CATEGORIES), 0.50f, avail);
            u32 cat_col = ink_over_art(t, COLOR_ALPHA(t->ink ? t->ink->text_dim : TEXT_DIM, (int)(235 * ui_a)),
                                       RGBA(175, 188, 210, (int)(230 * ui_a)), art_a);
            text_draw(kx, 49.0f, 0.50f, 0, cat_col, ALIGN_LEFT, cat_lbl);
        }
    }

    /* Korten – vertikalt balanserade vid få objekt och skrollbara vid fler än 2 rader. */
    int sel = ui->sel[TAB_LIBRARY];
    float focus_anim = smooth(ui->label_anim);
    float grid_y0 = v->count <= GRID_COLS * 2 ? GRID_Y + 14.0f : GRID_Y;
    int first_row = (int)floorf(ui->grid_scroll) - 1, last_row = (int)ceilf(ui->grid_scroll) + GRID_ROWS + 1;
    gfx_clip(0, GRID_Y - 8, SCREEN_W, GRID_BOTTOM - (GRID_Y - 8));
    float sx = 0, sy = 0, sw = 0, shh = 0;
    for (int p = first_row * GRID_COLS; p < (last_row + 1) * GRID_COLS && p < v->count; p++) {
        if (p < 0)
            continue;
        int r = p / GRID_COLS, c = p % GRID_COLS;
        float x = LIB_MARGIN + c * (CELL_W + GRID_GAP);
        float y = grid_y0 + (r - ui->grid_scroll) * ROW_STEP + slide;
        if (y > SCREEN_H || y + CELL_H < GRID_Y - 8)
            continue;
        if (p == sel) { /* ritas sist, ovanpå grannarna */
            sx = x; sy = y; sw = CELL_W; shh = CELL_H;
            continue;
        }
        float fade = clampf((GRID_BOTTOM - y) / CELL_H, 0, 1) * clampf((y + CELL_H - GRID_Y + 8) / CELL_H, 0, 1);
        float ta = a * (1 - launch) * fade;
        draw_library_tile(ui, v->idx[p], x, y, CELL_W, CELL_H, 0, ta, ta, art_a);
    }
    if (sw > 0) {
        float grow = 1 + 0.08f * focus_anim;
        float w = sw * grow, h = shh * grow;
        float cx_tile = sx + sw / 2, cy_tile = sy + shh / 2;
        if (launch > 0) { /* startanimation: kortet växer mot mitten */
            gfx_clip_reset();
            float g = 1 + launch * 2.4f;
            w *= g;
            h *= g;
            cx_tile = lerpf(cx_tile, SCREEN_W / 2, launch);
            cy_tile = lerpf(cy_tile, SCREEN_H / 2, launch);
        }
        draw_library_tile(ui, v->idx[sel], cx_tile - w / 2, cy_tile - h / 2, w, h,
                          0.4f + 0.6f * focus_anim, a, a * (1 - launch), art_a);
    }
    gfx_clip_reset();

    /* Vald titel med typetikett nere till vänster, synkad med footerns högerkant. */
    int i = ui_selected(ui);
    if (i >= 0) {
        static float max_w_cached;
        float bw = draw_meta_badge(LIB_MARGIN, 251.0f, type_name(ui->list->items[i].type), ui_a);
        float tx = LIB_MARGIN + bw + 8.0f;
        float max_w = SCREEN_W - LIB_MARGIN - hints_total_width(ui) - 14.0f - tx;
        if (max_w < 40.0f)
            max_w = 40.0f;
        if (ui->lib_label_for != i || fabsf(max_w_cached - max_w) > 1.0f) {
            text_ellipsis(ui->lib_label, sizeof(ui->lib_label), ui->list->items[i].title, 0.58f, max_w);
            ui->lib_label_for = i;
            max_w_cached = max_w;
        }
        text_draw(tx + (1 - focus_anim) * 6.0f, 260.5f, 0.58f, 0,
                  with_alpha(THEME_INK(t, text, TEXT_MAIN), ui_a * focus_anim), ALIGN_LEFT, ui->lib_label);
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

/* Blanda-symbolen: två korsade pilar, ca 18×12 px runt (cx, cy) */
static void draw_shuffle(float cx, float cy, u32 c)
{
    u32 cc[2] = { c, c };
    float l1[4] = { cx - 9, cy + 5, cx + 5, cy - 5 }, l2[4] = { cx - 9, cy - 5, cx + 5, cy + 5 };
    gfx_stroke(l1, cc, 2, 2);
    gfx_stroke(l2, cc, 2, 2);
    float up[6] = { cx + 9, cy - 5, cx + 4, cy - 8.5f, cx + 4, cy - 1.5f };
    float dn[6] = { cx + 9, cy + 5, cx + 4, cy + 1.5f, cx + 4, cy + 8.5f };
    gfx_fan(cx + 5.7f, cy - 5, up, 3, c);
    gfx_fan(cx + 5.7f, cy + 5, dn, 3, c);
}

/* Gemensam radrendering för både spårlistan och albumlistan i musikfliken */
static void draw_music_row(float x, float y, float w, const char *title, const char *sub,
                           int selected, int active, int show_eq, int playing, float time, float a, float art_a)
{
    const Theme *t = theme_cur();
    float rh = MUSIC_ROW_H - 2.0f;
    if (selected)
        theme_sel_box(t, x, y, w, rh, 7.0f, with_alpha(LIGHT, 0.95f * a));
    u32 main = selected ? with_alpha(THEME_INK(t, sel_ink, DARK), a)
                        : with_alpha(ink_over_art(t, t->ink ? t->ink->text : TEXT_MAIN, TEXT_MAIN, art_a), a);
    u32 dim = selected ? (t->ink ? with_alpha(t->ink->text_dim, a) : RGBA(74, 82, 96, (int)(255 * a)))
                       : ink_over_art(t, COLOR_ALPHA(t->ink ? t->ink->text_dim : TEXT_DIM, (int)(245 * a)),
                                      RGBA(134, 146, 166, (int)(240 * a)), art_a);
    u32 tile = active
        ? (t->ink ? with_alpha(t->ink->graphite, a)
                  : (g_theme == 0 ? RGBA(232, 62, 62, (int)(255 * a)) : with_alpha(t->accent, a)))
        : (t->ink ? COLOR_ALPHA(t->ink->panel_alt, (int)(245 * a))
                  : (selected ? RGBA(44, 52, 68, (int)(235 * a)) : RGBA(31, 38, 50, (int)(238 * a))));
    u32 note = active ? with_alpha(THEME_INK(t, text_inverse, LIGHT), a)
        : (t->ink ? with_alpha(selected ? t->ink->graphite : t->ink->text_dim, a)
                  : (selected ? with_alpha(LIGHT, 0.90f * a) : RGBA(154, 166, 186, (int)(225 * a))));
    float tx = x + 7.0f, ty = y + (rh - 18.0f) * 0.5f;
    gfx_round_rect(tx, ty, 18.0f, 18.0f, 4.0f, tile);
    draw_note(tx + 9.0f, ty + 9.0f, 10.0f, note);
    if (show_eq) {
        float ex = x + w - 20.0f;
        for (int k = 0; k < 3; k++) {
            float hgt = playing ? 3.5f + 7.5f * (0.5f + 0.5f * sinf(time * (7.0f + k * 2.3f) + k)) : 3.5f;
            gfx_rect(ex + k * 4.0f, y + rh - 6.5f - hgt, 2.5f, hgt, main);
        }
    }
    text_draw(x + 31.0f, y + 11.5f, 0.58f, 0, main, ALIGN_LEFT, title);
    text_draw(x + 31.0f, y + 21.5f, 0.45f, 0, dim, ALIGN_LEFT, sub);
}

static void draw_transport_step(float cx, float cy, int dir, int active, float a)
{
    const Theme *t = theme_cur();
    u32 bg = active ? (t->ink ? with_alpha(t->ink->graphite, a) : with_alpha(t->accent, 0.88f * a))
                    : (t->ink ? COLOR_ALPHA(t->ink->panel_alt, (int)(242 * a)) : RGBA(37, 45, 58, (int)(238 * a)));
    u32 fg = t->ink ? (active ? with_alpha(t->ink->text_inverse, a) : with_alpha(t->ink->graphite, a))
                    : with_alpha(LIGHT, 0.94f * a);
    gfx_round_rect(cx - 12.0f, cy - 9.0f, 24.0f, 18.0f, 4.0f, bg);
    draw_seek_glyph(cx, cy, 4.2f, dir, fg);
}

/* Albumlistan i musikfliken: först "Alla låtar", sedan ett album per rad (namn, artist · antal) */
static void draw_album_rows(Ui *ui, float a, float slide, float art_a)
{
    static RowCache cache[MUSIC_ROWS + 3];
    static int cache_gen = -1, cache_n = -1;
    static const char *cache_lang;
    if (cache_gen != s_alb_gen || cache_n != s_alb_count || cache_lang != L(S_ALL_TRACKS)) {
        for (int i = 0; i < MUSIC_ROWS + 3; i++)
            cache[i].idx = -1;
        cache_gen = s_alb_gen;
        cache_n = s_alb_count;
        cache_lang = L(S_ALL_TRACKS);
    }
    float max_tw = MUSIC_LIST_W - 42.0f;
    int first = (int)floorf(ui->album_scroll), last = first + MUSIC_ROWS;
    for (int i = first < 0 ? 0 : first; i <= last && i <= s_nalb; i++) {
        float y = MUSIC_LIST_Y + (i - ui->album_scroll) * MUSIC_ROW_H + slide;
        RowCache *c = &cache[i % (MUSIC_ROWS + 3)];
        if (c->idx != i) {
            const Track *t = i ? music_track(s_alb_first[i - 1]) : NULL;
            int cnt = i ? s_alb_first[i] - s_alb_first[i - 1] : s_alb_count;
            char sub[96];
            if (t)
                snprintf(sub, sizeof(sub), "%s  ·  %d", t->artist[0] ? t->artist : L(S_UNKNOWN_ARTIST), cnt);
            else
                snprintf(sub, sizeof(sub), "%d", cnt);
            /* låtar direkt i MUSIC-mappen har inget album */
            text_ellipsis(c->title, sizeof(c->title), !t ? L(S_ALL_TRACKS) : t->album[0] ? t->album : L(S_TAB_MUSIC),
                          0.58f, max_tw);
            text_ellipsis(c->sub, sizeof(c->sub), sub, 0.45f, max_tw);
            c->idx = i;
        }
        draw_music_row(MUSIC_LIST_X, y, MUSIC_LIST_W, c->title, c->sub,
                       i == ui->album_sel, i == ui->music_album, 0, 0, ui->time, a, art_a);
    }
}

static void draw_music(Ui *ui)
{
    const Theme *th = theme_cur();
    float art_a = ui_art_alpha(ui);
    float a = ui->intro * smooth(ui->tab_anim);
    float slide = (1 - smooth(ui->tab_anim)) * 10;
    int n = music_count(), cur = music_current(), state = music_state();

    /* --- höger kort: "spelas nu" --- */
    float px = MUSIC_PANEL_X, pw = SCREEN_W - LIB_MARGIN - MUSIC_PANEL_X;
    float py = MUSIC_LIST_Y + slide, ph = MUSIC_PANEL_H;
    u32 border = th->ink      ? COLOR_ALPHA(th->ink->border, (int)(235 * a))
               : g_theme == 0 ? RGBA(44, 55, 74, (int)(210 * a))
                              : with_alpha(th->accent, 0.35f * a);
    u32 panel_bg = th->ink      ? COLOR_ALPHA(th->ink->panel, (int)(242 * a))
                 : g_theme == 0 ? RGBA(18, 24, 34, (int)(238 * a))
                                : with_alpha(th->card, 0.90f * a);
    gfx_round_rect(px, py, pw, ph, 10.0f, border);
    gfx_round_rect(px + 1.0f, py + 1.0f, pw - 2.0f, ph - 2.0f, 9.2f, panel_bg);
    text_draw(px + 12.0f, py + 16.5f, 0.54f, 0, with_alpha(THEME_INK(th, text, TEXT_MAIN), a), ALIGN_LEFT, L(S_NOW_PLAYING));
    u32 shuf_on = th->ink ? with_alpha(th->ink->sym[THEME_SYM_CROSS], a) : with_alpha(th->accent, a);
    u32 shuf_off = th->ink ? COLOR_ALPHA(th->ink->text_muted, (int)(160 * a)) : RGBA(140, 152, 172, (int)(75 * a));
    draw_shuffle(px + pw - 20.0f, py + 12.5f, g_shuffle ? shuf_on : shuf_off);
    gfx_rect(px + 10.0f, py + 25.0f, pw - 20.0f, 1.0f,
             th->ink      ? COLOR_ALPHA(th->ink->border, (int)(200 * a))
           : g_theme == 0 ? RGBA(44, 55, 74, (int)(190 * a))
                          : RGBA(255, 255, 255, (int)(28 * a)));

    const Track *t = cur >= 0 ? music_track(cur) : NULL;
    float as = 72.0f, ax = px + (pw - as) * 0.5f, ay = py + 33.0f;
    unsigned h = (cur >= 0 ? (unsigned)cur : 7u) * 2654435761u;
    int r0 = 60 + (h >> 8) % 120, g0 = 60 + (h >> 16) % 110, b0 = 90 + (h >> 24) % 140;
    gfx_glow(ax, ay + 3.0f, as, as, 8.0f, RGBA(0, 0, 0, (int)(110 * a)));
    u32 art_bg = t ? RGBA(r0, g0, b0, (int)(255 * a))
                   : (th->ink ? COLOR_ALPHA(th->ink->panel_alt, (int)(235 * a)) : RGBA(30, 38, 52, (int)(205 * a)));
    gfx_round_rect(ax, ay, as, as, 8.0f, art_bg);
    if (t)
        gfx_gradient_v(ax + 4.0f, ay + as * 0.5f, as - 8.0f, as * 0.5f - 4.0f,
                       RGBA(0, 0, 0, 0), RGBA(0, 0, 0, (int)(65 * a)));
    u32 note_empty = (!t && th->ink) ? COLOR_ALPHA(th->ink->text_muted, (int)(185 * a))
                                     : RGBA(255, 255, 255, (int)((t ? 230 : 105) * a));
    draw_note(ax + as * 0.5f, ay + as * 0.5f, 30.0f, note_empty);
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
        text_ellipsis(ptitle, sizeof(ptitle), t ? t->title : L(S_NOTHING_PLAYING), 0.58f, pw - 20.0f);
        text_ellipsis(partist, sizeof(partist), t ? (t->artist[0] ? t->artist : L(S_UNKNOWN_ARTIST)) : "", 0.46f, pw - 20.0f);
        pfor = cur;
    }
    text_draw(px + pw * 0.5f, ay + as + 16.0f, 0.58f, 0, with_alpha(THEME_INK(th, text, TEXT_MAIN), a), ALIGN_CENTER, ptitle);
    u32 sub_col = th->ink ? COLOR_ALPHA(th->ink->text_dim, (int)(245 * a)) : RGBA(148, 163, 184, (int)(235 * a));
    text_draw(px + pw * 0.5f, ay + as + 30.0f, 0.46f, 0, sub_col, ALIGN_CENTER, partist);

    if (t) {
        int seek = ui->seeking || ui->seek_show > 0;
        float el = seek ? ui->seek_pos : music_elapsed(), du = music_duration();
        float bw = pw - 28.0f, bxx = px + 14.0f, by = py + ph - 53.0f;
        gfx_round_rect(bxx, by, bw, 3.0f, 1.5f,
                       th->ink ? COLOR_ALPHA(th->ink->panel_deep, (int)(240 * a)) : RGBA(44, 56, 74, (int)(220 * a)));
        if (du > 0) {
            float fw = fmaxf(3.0f, bw * clampf(el / du, 0.0f, 1.0f));
            u32 bar_fg = THEME_INK(th, graphite, LIGHT);
            gfx_round_rect(bxx, by, fw, 3.0f, 1.5f, with_alpha(bar_fg, 0.96f * a));
            gfx_circle(bxx + fw, by + 1.5f, ui->seeking ? 4.2f : 2.8f, with_alpha(bar_fg, a));
        }
        char e[12], d[12];
        format_time(e, sizeof(e), el);
        format_time(d, sizeof(d), du);
        u32 ts_col = th->ink ? COLOR_ALPHA(th->ink->text_dim, (int)(240 * a)) : RGBA(140, 152, 172, (int)(235 * a));
        text_draw(bxx, by + 13.0f, 0.44f, 0, ts_col, ALIGN_LEFT, e);
        if (du > 0)
            text_draw(bxx + bw, by + 13.0f, 0.44f, 0, ts_col, ALIGN_RIGHT, d);
        float tx = px + pw * 0.5f, ty = py + ph - 21.0f;
        draw_transport_step(tx - 34.0f, ty, -1, ui->seeking && ui->seek_dir < 0, a);
        gfx_circle(tx, ty, 10.5f, with_alpha(THEME_INK(th, graphite, LIGHT), 0.96f * a));
        u32 glyph_fg = THEME_INK(th, text_inverse, DARK);
        if (ui->seeking)
            draw_seek_glyph(tx, ty, 5.2f, ui->seek_dir, with_alpha(glyph_fg, a));
        else
            draw_play_glyph(tx, ty, 6.2f, state == MUSIC_PLAYING, with_alpha(glyph_fg, a));
        draw_transport_step(tx + 34.0f, ty, 1, ui->seeking && ui->seek_dir > 0, a);
    }

    float left_max = MUSIC_PANEL_X - LIB_MARGIN - 16.0f;
    u32 list_main = ink_over_art(th, th->ink ? th->ink->text : TEXT_MAIN, TEXT_MAIN, art_a);
    u32 list_dim = ink_over_art(th, th->ink ? th->ink->text_dim : TEXT_DIM, TEXT_DIM, art_a);
    if (!n && music_scanning()) {
        float tsz = text_fit(0.85f, 0.55f, 0, left_max, L(S_LOADING_MUSIC));
        text_draw(LIB_MARGIN, 112.0f, tsz, 0, with_alpha(list_main, a), ALIGN_LEFT, L(S_LOADING_MUSIC));
        for (int i = 0; i < 3; i++) { /* tre punkter som pulserar i tur och ordning */
            float p = 0.5f + 0.5f * sinf(ui->time * 6.0f - i * 0.9f);
            gfx_circle(LIB_MARGIN + 4 + i * 12, 134.0f, 2 + p, COLOR_ALPHA(th->accent, (int)((90 + 165 * p) * a)));
        }
        return;
    }
    if (!n) {
        const char *hint = on_root(L(S_HINT_MUSIC));
        float tsz = text_fit(0.85f, 0.55f, 0, left_max, L(S_NO_MUSIC));
        float hsz = text_fit(0.56f, 0.38f, 0, left_max, hint);
        text_draw(LIB_MARGIN, 112.0f, tsz, 0, with_alpha(list_main, a), ALIGN_LEFT, L(S_NO_MUSIC));
        text_draw(LIB_MARGIN, 134.0f, hsz, 0, with_alpha(list_dim, a), ALIGN_LEFT, hint);
        return;
    }

    /* --- vänster lista: låtar eller album --- */
    static RowCache cache[MUSIC_ROWS + 3];
    static int cache_gen = -1, cache_cur = -2, cache_stopped = -1;
    static const char *cache_lang;
    int stopped = state == MUSIC_STOPPED;
    if (cache_gen != music_generation() || cache_lang != L(S_UNKNOWN_ARTIST)
        || cache_cur != cur || cache_stopped != stopped) {
        for (int i = 0; i < MUSIC_ROWS + 3; i++)
            cache[i].idx = -1;
        cache_gen = music_generation();
        cache_lang = L(S_UNKNOWN_ARTIST);
        cache_cur = cur;
        cache_stopped = stopped;
    }
    gfx_clip(0, MUSIC_LIST_Y - 2.0f, MUSIC_PANEL_X - 8.0f, MUSIC_ROWS * MUSIC_ROW_H + 2.0f);
    int lo, hi;
    music_range(ui, &lo, &hi);
    if (ui->music_view)
        draw_album_rows(ui, a, slide, art_a);
    int first = (int)floorf(ui->music_scroll), last = first + MUSIC_ROWS;
    for (int i = first; !ui->music_view && i <= last && i < hi; i++) {
        if (i < lo)
            continue;
        float y = MUSIC_LIST_Y + (i - ui->music_scroll) * MUSIC_ROW_H + slide;
        RowCache *c = &cache[i % (MUSIC_ROWS + 3)];
        const Track *tr = music_track(i);
        if (!tr)
            continue;
        int active = i == cur && state != MUSIC_STOPPED;
        if (c->idx != i) {
            float max_tw = MUSIC_LIST_W - (active ? 56.0f : 42.0f);
            text_ellipsis(c->title, sizeof(c->title), tr->title, 0.58f, max_tw);
            text_ellipsis(c->sub, sizeof(c->sub), tr->artist[0] ? tr->artist : L(S_UNKNOWN_ARTIST), 0.45f, max_tw);
            c->idx = i;
        }
        draw_music_row(MUSIC_LIST_X, y, MUSIC_LIST_W, c->title, c->sub,
                       i == ui->music_sel, active, active, state == MUSIC_PLAYING, ui->time, a, art_a);
    }
    gfx_clip_reset();

    /* knapptips nere till vänster i fotraden */
    float x = LIB_MARGIN, y = 257.0f;
    u32 label_col = th->ink ? COLOR_ALPHA(th->ink->text, (int)(245 * a)) : RGBA(218, 224, 235, (int)(238 * a));
    u32 active_lbl = th->ink ? with_alpha(th->ink->sym[THEME_SYM_CROSS], a) : with_alpha(th->accent, a);
    const char *play = L(ui->music_view ? S_SELECT : ui->music_sel == cur && state == MUSIC_PLAYING ? S_PAUSE : S_PLAY);
    theme_button(th, x + 6.5f, y, 6.5f, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, 0,
                 with_alpha(LIGHT, 0.96f * a), with_alpha(DARK, a));
    x += 17.0f;
    text_draw(x, y + 4.5f, 0.54f, 0, label_col, ALIGN_LEFT, play);
    x += text_width(0.54f, 0, play) + 10.0f;
    theme_button(th, x + 6.5f, y, 6.5f, BTN_SQUARE, 0, with_alpha(LIGHT, 0.96f * a), with_alpha(DARK, a));
    x += 17.0f;
    text_draw(x, y + 4.5f, 0.54f, 0, g_shuffle ? active_lbl : label_col, ALIGN_LEFT, L(S_SHUFFLE));
    x += text_width(0.54f, 0, L(S_SHUFFLE)) + 10.0f;
    theme_button(th, x + 6.5f, y, 6.5f, BTN_TRIANGLE, 0, with_alpha(LIGHT, 0.96f * a), with_alpha(DARK, a));
    x += 17.0f;
    text_draw(x, y + 4.5f, 0.54f, 0, ui->music_view ? active_lbl : label_col, ALIGN_LEFT, L(S_ALBUMS));
    x += text_width(0.54f, 0, L(S_ALBUMS)) + 10.0f;
    u32 arr = THEME_INK(th, graphite, LIGHT);
    float l[6] = { x, y, x + 6.0f, y - 4.5f, x + 6.0f, y + 4.5f };
    gfx_fan(x + 3.0f, y, l, 3, with_alpha(arr, 0.94f * a));
    float r[6] = { x + 15.0f, y, x + 9.0f, y - 4.5f, x + 9.0f, y + 4.5f };
    gfx_fan(x + 12.0f, y, r, 3, with_alpha(arr, 0.94f * a));
    float max_tw = SCREEN_W - LIB_MARGIN - hints_total_width(ui) - 14.0f - (x + 19.0f);
    float tsize = text_fit(0.54f, 0.38f, 0, max_tw, L(S_TRACK));
    text_draw(x + 19.0f, y + 4.5f, tsize, 0, label_col, ALIGN_LEFT, L(S_TRACK));
}

/* Bildfliken: bildlista till vänster, förhandsvisning med upplösning till höger, helskärm med ✕ */
static void draw_pictures(Ui *ui)
{
    const Theme *th = theme_cur();
    float art_a = ui_art_alpha(ui);
    float a = ui->intro * smooth(ui->tab_anim);
    float slide = (1 - smooth(ui->tab_anim)) * 10;
    int n = s_npics;

    /* --- förhandsvisning till höger --- */
    float px = PANEL_X, pw = SCREEN_W - MARGIN - PANEL_X, py = MLIST_Y + slide, ph = MROWS * MROW_H - 2;
    if (th->ink) {
        gfx_round_rect(px, py, pw, ph, 10, COLOR_ALPHA(th->ink->border, (int)(235 * a)));
        gfx_round_rect(px + 1, py + 1, pw - 2, ph - 2, 9.2f, COLOR_ALPHA(th->ink->panel, (int)(242 * a)));
    } else {
        gfx_round_rect(px, py, pw, ph, 10, RGBA(255, 255, 255, (int)(16 * a)));
    }
    float bx = px + 10, by = py + 10, bw = pw - 20, bh = ph - 44;
    gfx_round_rect(bx, by, bw, bh, 7, RGBA(18, 19, 24, (int)(200 * a)));

    u32 main_fg = ink_over_art(th, th->ink ? th->ink->text : TEXT_MAIN, TEXT_MAIN, art_a);
    u32 dim_fg = ink_over_art(th, th->ink ? th->ink->text_dim : TEXT_DIM, TEXT_DIM, art_a);
    u32 cap_col = THEME_INK(th, text_dim, TEXT_DIM);

    if (n > 0 && ui->pic_tex && ui->pic_tex_for == ui->pic_sel) {
        Texture *tx = ui->pic_tex;
        float pa = smooth(ui->pic_fade);
        float s = fminf(bw / tx->w, bh / tx->h);
        float iw = tx->w * s, ih = tx->h * s;
        float ix = bx + (bw - iw) / 2, iy = by + (bh - ih) / 2;
        gfx_glow(ix, iy + 2, iw, ih, 6, RGBA(0, 0, 0, (int)(120 * a * pa)));
        gfx_tex(tx, ix, iy, iw, ih, RGBA(255, 255, 255, (int)(255 * a * pa)));
        char info[96];
        snprintf(info, sizeof(info), "%d × %d  ·  %d / %d", tx->w, tx->h, ui->pic_sel + 1, n);
        text_draw(px + pw / 2, py + ph - 14, 0.48f, 0, with_alpha(cap_col, a), ALIGN_CENTER, info);
    } else if (n > 0) {
        char info[48];
        snprintf(info, sizeof(info), "%d / %d", ui->pic_sel + 1, n);
        text_draw(px + pw / 2, py + ph - 14, 0.48f, 0, with_alpha(cap_col, a), ALIGN_CENTER, info);
    }

    if (!n && media_loading()) {
        text_draw(MARGIN, 120, 0.9f, 0, with_alpha(main_fg, a), ALIGN_LEFT, L(S_LOADING_MEDIA));
        return;
    }
    if (!n) {
        text_draw(MARGIN, 120, 0.9f, 0, with_alpha(main_fg, a), ALIGN_LEFT, L(S_NO_PICTURES));
        text_draw(MARGIN, 142, 0.62f, 0, with_alpha(dim_fg, a), ALIGN_LEFT, on_root(L(S_HINT_PICTURES)));
        return;
    }

    /* --- bildlistan till vänster --- */
    float lw = PANEL_X - 14 - MARGIN;
    gfx_clip(0, MLIST_Y - 2, PANEL_X - 6, MROWS * MROW_H + 2);
    int first = (int)floorf(ui->pic_scroll), last = first + MROWS;
    for (int i = first; i <= last && i < n; i++) {
        if (i < 0)
            continue;
        float y = MLIST_Y + (i - ui->pic_scroll) * MROW_H + slide;
        int selected = i == ui->pic_sel;
        if (selected)
            theme_sel_box(th, MARGIN - 6, y, lw + 12, MROW_H - 2, 7, with_alpha(LIGHT, 0.93f * a));
        u32 main = selected ? with_alpha(THEME_INK(th, sel_ink, DARK), a) : with_alpha(main_fg, a);
        u32 dim = selected ? (th->ink ? with_alpha(th->ink->text_dim, a) : RGBA(70, 72, 80, (int)(255 * a)))
                           : with_alpha(dim_fg, a);

        /* liten bildikon till vänster */
        int is_wall = g_wallpaper[0] && !strcmp(g_wallpaper, s_pics[i].path);
        u32 ic = is_wall ? (th->ink ? with_alpha(th->ink->sym[THEME_SYM_CROSS], a) : with_alpha(th->accent, a)) : dim;
        gfx_round_rect(MARGIN + 1, y + 7, 13, 10, 2, ic);
        u32 dot_col = selected ? with_alpha(THEME_INK(th, panel_hi, LIGHT), a)
                               : (th->ink ? with_alpha(th->ink->text_inverse, a) : RGBA(24, 25, 30, (int)(255 * a)));
        gfx_circle(MARGIN + 5, y + 10.5f, 1.3f, dot_col);

        char title[100], sub[72];
        text_ellipsis(title, sizeof(title), s_pics[i].title, 0.6f, lw - 30);
        text_ellipsis(sub, sizeof(sub), s_pics[i].sub, 0.46f, lw - 30);
        text_draw(MARGIN + 20, y + 11.5f, 0.6f, 0, main, ALIGN_LEFT, title);
        text_draw(MARGIN + 20, y + 22, 0.46f, 0, dim, ALIGN_LEFT, sub);
    }
    gfx_clip_reset();

    /* knapptips nere till vänster: ✕ Öppna  □ Som bakgrundsbild */
    float x = MARGIN, y = 246;
    const char *open = L(S_F_OPEN), *wall = L(S_SET_WALLPAPER);
    theme_button(th, x + 6.5f, y, 6.5f, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, art_a > 0.3f,
                 with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    x += 17;
    text_draw(x, y + 4.5f, 0.56f, 0, with_alpha(main_fg, a), ALIGN_LEFT, open);
    x += text_width(0.56f, 0, open) + 12;
    theme_button(th, x + 6.5f, y, 6.5f, BTN_SQUARE, art_a > 0.3f, with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    x += 17;
    float wsize = text_fit(0.56f, 0.4f, 0, SCREEN_W - MARGIN - hints_width() - 22 - x, wall);
    text_draw(x, y + 4.5f, wsize, 0, with_alpha(main_fg, a), ALIGN_LEFT, wall);
}

static void draw_pic_fullscreen(Ui *ui)
{
    const Theme *th = theme_cur();
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(8, 8, 12, 248));
    if (ui->pic_tex) {
        Texture *tx = ui->pic_tex;
        float s = fminf((float)SCREEN_W / tx->w, (float)SCREEN_H / tx->h);
        float w = tx->w * s, h = tx->h * s;
        gfx_tex(tx, (SCREEN_W - w) / 2, (SCREEN_H - h) / 2, w, h, RGBA(255, 255, 255, 255));
    }
    /* liten knapprad längst ner: ○ Tillbaka  □ Som bakgrundsbild  ◀ ▶ */
    const char *back = L(S_BACK), *wall = L(S_SET_WALLPAPER);
    char cnt[32];
    snprintf(cnt, sizeof(cnt), "%d / %d", ui->pic_sel + 1, s_npics);
    float w = 17 + text_width(0.52f, 0, back) + 14 + 17 + text_width(0.52f, 0, wall) + 14 + text_width(0.52f, 0, cnt);
    float x = MARGIN, y = 248;
    gfx_round_rect(x - 8, y - 10, w + 16, 20, 10, RGBA(0, 0, 0, 165));
    u32 fg = THEME_INK(th, text_inverse, TEXT_MAIN);
    u32 dim = th->ink ? th->ink->panel_alt : TEXT_DIM;
    theme_button(th, x + 6, y, 6, ui->confirm_circle ? BTN_CROSS : BTN_CIRCLE, 1, LIGHT, DARK);
    x += 16;
    text_draw(x, y + 4, 0.52f, 0, fg, ALIGN_LEFT, back);
    x += text_width(0.52f, 0, back) + 14;
    theme_button(th, x + 6, y, 6, BTN_SQUARE, 1, LIGHT, DARK);
    x += 16;
    text_draw(x, y + 4, 0.52f, 0, fg, ALIGN_LEFT, wall);
    x += text_width(0.52f, 0, wall) + 14;
    text_draw(x, y + 4, 0.52f, 0, dim, ALIGN_LEFT, cnt);
}

/* Videofliken: videolista till vänster, PMF/MPS-uppspelning till höger, helskärm med ✕ */
static void draw_videos(Ui *ui)
{
    const Theme *th = theme_cur();
    float art_a = ui_art_alpha(ui);
    float a = ui->intro * smooth(ui->tab_anim);
    float slide = (1 - smooth(ui->tab_anim)) * 10;
    int n = s_nvids;

    /* --- videoförhandsvisning till höger --- */
    float px = PANEL_X, pw = SCREEN_W - MARGIN - PANEL_X, py = MLIST_Y + slide, ph = MROWS * MROW_H - 2;
    if (th->ink) {
        gfx_round_rect(px, py, pw, ph, 10, COLOR_ALPHA(th->ink->border, (int)(235 * a)));
        gfx_round_rect(px + 1, py + 1, pw - 2, ph - 2, 9.2f, COLOR_ALPHA(th->ink->panel, (int)(242 * a)));
    } else {
        gfx_round_rect(px, py, pw, ph, 10, RGBA(255, 255, 255, (int)(16 * a)));
    }
    float bx = px + 10, by = py + 10, bw = pw - 20, bh = ph - 44;
    gfx_round_rect(bx, by, bw, bh, 7, RGBA(18, 19, 24, (int)(210 * a)));

    u32 main_fg = ink_over_art(th, th->ink ? th->ink->text : TEXT_MAIN, TEXT_MAIN, art_a);
    u32 dim_fg = ink_over_art(th, th->ink ? th->ink->text_dim : TEXT_DIM, TEXT_DIM, art_a);
    u32 cap_col = THEME_INK(th, text_dim, TEXT_DIM);

    if (n > 0 && ui->vid_sel < n) {
        Texture *vt = ui->preview_anim > 0 ? preview_frame(VID_PREVIEW + ui->vid_sel) : NULL;
        float va = smooth(ui->preview_anim);
        if (!vt && ui->vid_thumb && ui->vid_thumb_for == ui->vid_sel) {
            vt = ui->vid_thumb; /* MP4: .THM-bilden */
            va = 1;
        }
        if (vt) {
            float s = fminf(bw / vt->w, bh / vt->h);
            float iw = vt->w * s, ih = vt->h * s;
            float ix = bx + (bw - iw) / 2, iy = by + (bh - ih) / 2;
            gfx_glow(ix, iy + 2, iw, ih, 6, RGBA(0, 0, 0, (int)(130 * a * va)));
            gfx_tex(vt, ix, iy, iw, ih, RGBA(255, 255, 255, (int)(255 * a * va)));
        } else {
            gfx_circle(bx + bw / 2, by + bh / 2, 13, RGBA(0, 0, 0, (int)(140 * a)));
            draw_play_glyph(bx + bw / 2 + 1, by + bh / 2, 7.5f, 0,
                            with_alpha(THEME_INK(th, text_inverse, LIGHT), 0.9f * a));
        }
        char info[64];
        int kind = s_vids[ui->vid_sel].kind;
        if (kind == VID_MP4) /* bildens storlek är .THM-bildens, inte videons */
            snprintf(info, sizeof(info), "MP4  ·  %d / %d", ui->vid_sel + 1, n);
        else if (kind == VID_UMD_ISO || kind == VID_UMD_DISC) /* spelas i XMB */
            snprintf(info, sizeof(info), "UMD Video  ·  XMB  ·  %d / %d", ui->vid_sel + 1, n);
        else if (vt)
            snprintf(info, sizeof(info), "%d × %d  ·  %d / %d", vt->w, vt->h, ui->vid_sel + 1, n);
        else
            snprintf(info, sizeof(info), "%d / %d", ui->vid_sel + 1, n);
        text_draw(px + pw / 2, py + ph - 14, 0.48f, 0, with_alpha(cap_col, a), ALIGN_CENTER, info);
    }

    if (!n && media_loading()) {
        text_draw(MARGIN, 120, 0.9f, 0, with_alpha(main_fg, a), ALIGN_LEFT, L(S_LOADING_MEDIA));
        return;
    }
    if (!n) {
        text_draw(MARGIN, 120, 0.9f, 0, with_alpha(main_fg, a), ALIGN_LEFT, L(S_NO_VIDEOS));
        text_draw(MARGIN, 142, 0.62f, 0, with_alpha(dim_fg, a), ALIGN_LEFT, on_root(L(S_HINT_VIDEOS)));
        return;
    }

    /* --- videolistan till vänster --- */
    float lw = PANEL_X - 14 - MARGIN;
    gfx_clip(0, MLIST_Y - 2, PANEL_X - 6, MROWS * MROW_H + 2);
    int first = (int)floorf(ui->vid_scroll), last = first + MROWS;
    for (int i = first; i <= last && i < n; i++) {
        if (i < 0)
            continue;
        float y = MLIST_Y + (i - ui->vid_scroll) * MROW_H + slide;
        int selected = i == ui->vid_sel;
        if (selected)
            theme_sel_box(th, MARGIN - 6, y, lw + 12, MROW_H - 2, 7, with_alpha(LIGHT, 0.93f * a));
        u32 main = selected ? with_alpha(THEME_INK(th, sel_ink, DARK), a) : with_alpha(main_fg, a);
        u32 dim = selected ? (th->ink ? with_alpha(th->ink->text_dim, a) : RGBA(70, 72, 80, (int)(255 * a)))
                           : with_alpha(dim_fg, a);

        gfx_round_rect(MARGIN + 1, y + 7, 13, 10, 2, dim);
        u32 tri_col = selected ? with_alpha(THEME_INK(th, panel_hi, LIGHT), a)
                               : (th->ink ? with_alpha(th->ink->text_inverse, a) : RGBA(24, 25, 30, (int)(255 * a)));
        draw_play_glyph(MARGIN + 7.8f, y + 12.0f, 3.6f, 0, tri_col);

        char title[100], sub[72];
        text_ellipsis(title, sizeof(title), s_vids[i].title, 0.6f, lw - 30);
        text_ellipsis(sub, sizeof(sub), s_vids[i].sub, 0.46f, lw - 30);
        text_draw(MARGIN + 20, y + 11.5f, 0.6f, 0, main, ALIGN_LEFT, title);
        text_draw(MARGIN + 20, y + 22, 0.46f, 0, dim, ALIGN_LEFT, sub);
    }
    gfx_clip_reset();

    /* knapptips nere till vänster: ✕ Öppna  ○ Tillbaka */
    float x = MARGIN, y = 246;
    const char *open = L(S_F_OPEN), *back = L(S_BACK);
    theme_button(th, x + 6.5f, y, 6.5f, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, art_a > 0.3f,
                 with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    x += 17;
    text_draw(x, y + 4.5f, 0.56f, 0, with_alpha(main_fg, a), ALIGN_LEFT, open);
    x += text_width(0.56f, 0, open) + 12;
    theme_button(th, x + 6.5f, y, 6.5f, ui->confirm_circle ? BTN_CROSS : BTN_CIRCLE, art_a > 0.3f,
                 with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    x += 17;
    text_draw(x, y + 4.5f, 0.56f, 0, with_alpha(main_fg, a), ALIGN_LEFT, back);
}

static void draw_vid_fullscreen(Ui *ui)
{
    const Theme *th = theme_cur();
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(6, 6, 10, 252));
    Texture *vt = preview_frame(VID_PREVIEW + ui->vid_sel);
    if (!vt && ui->vid_thumb_for == ui->vid_sel)
        vt = ui->vid_thumb; /* ← → kan stanna på en MP4: visa dess bild */
    if (vt) {
        float s = fminf((float)SCREEN_W / vt->w, (float)SCREEN_H / vt->h);
        float w = vt->w * s, h = vt->h * s;
        gfx_tex(vt, (SCREEN_W - w) / 2, (SCREEN_H - h) / 2, w, h, RGBA(255, 255, 255, 255));
    }
    const char *back = L(S_BACK);
    char cnt[32];
    snprintf(cnt, sizeof(cnt), "%d / %d", ui->vid_sel + 1, s_nvids);
    float w = 17 + text_width(0.52f, 0, back) + 14 + text_width(0.52f, 0, cnt);
    float x = MARGIN, y = 248;
    gfx_round_rect(x - 8, y - 10, w + 16, 20, 10, RGBA(0, 0, 0, 165));
    theme_button(th, x + 6, y, 6, ui->confirm_circle ? BTN_CROSS : BTN_CIRCLE, 1, LIGHT, DARK);
    x += 16;
    text_draw(x, y + 4, 0.52f, 0, THEME_INK(th, text_inverse, TEXT_MAIN), ALIGN_LEFT, back);
    x += text_width(0.52f, 0, back) + 14;
    text_draw(x, y + 4, 0.52f, 0, th->ink ? th->ink->panel_alt : TEXT_DIM, ALIGN_LEFT, cnt);
}

/* ✕ Välj  ○ Tillbaka – längst ner i panelerna */
static void draw_select_back(Ui *ui, float x, float y, int on_dark, float a)
{
    const Theme *th = theme_cur();
    u32 fg = on_dark ? THEME_INK(th, text_inverse, TEXT_MAIN)
                     : ink_over_art(th, th->ink ? th->ink->text : TEXT_MAIN, TEXT_MAIN, ui_art_alpha(ui));
    theme_button(th, x + 6.5f, y, 6.5f, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, on_dark,
                 with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    x += 17;
    text_draw(x, y + 4.5f, 0.52f, 0, with_alpha(fg, a), ALIGN_LEFT, L(S_SELECT));
    x += text_width(0.52f, 0, L(S_SELECT)) + 12;
    theme_button(th, x + 6.5f, y, 6.5f, ui->confirm_circle ? BTN_CROSS : BTN_CIRCLE, on_dark,
                 with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    text_draw(x + 17, y + 4.5f, 0.52f, 0, with_alpha(fg, a), ALIGN_LEFT, L(S_BACK));
}

/* Sökfliken (🔍): PS5-sökfält med tangentrad och direktfiltrerade spel/appar */
static void draw_search(Ui *ui)
{
    const Theme *th = theme_cur();
    float art_a = ui_art_alpha(ui);
    float a = ui->intro * smooth(ui->tab_anim);
    float slide = (1 - smooth(ui->tab_anim)) * 8;
    float launch = ui->launch < 0 ? 0 : smooth(ui->launch);
    a *= 1 - smooth(launch * 2);

    int hits[512];
    int n = collect_search(ui, hits, 512);

    /* sökfältet högst upp */
    float sx = MARGIN, sy = 42 + slide, sw = SCREEN_W - 2 * MARGIN, sh = 24;
    if (th->ink) {
        u32 edge = ui->search_focus == 0 ? th->ink->sel_edge : th->ink->border;
        u32 fill = ui->search_focus == 0 ? th->ink->sel_fill : th->ink->panel;
        gfx_round_rect(sx, sy, sw, sh, 12, COLOR_ALPHA(edge, (int)(245 * a)));
        gfx_round_rect(sx + 1, sy + 1, sw - 2, sh - 2, 11, COLOR_ALPHA(fill, (int)(245 * a)));
    } else {
        gfx_round_rect(sx, sy, sw, sh, 12, RGBA(255, 255, 255, (int)((ui->search_focus == 0 ? 28 : 18) * a)));
        if (ui->search_focus == 0)
            gfx_ring(sx, sy, sw, sh, 12, RGBA(255, 255, 255, (int)(130 * a)));
    }
    draw_search_icon(sx + 14, sy + 12, with_alpha(THEME_INK(th, text, TEXT_MAIN), a));

    if (ui->search_query[0]) {
        text_draw(sx + 28, sy + 16.5f, 0.62f, 0, with_alpha(THEME_INK(th, text, TEXT_MAIN), a), ALIGN_LEFT, ui->search_query);
        float qw = text_width(0.62f, 0, ui->search_query);
        if (ui->search_focus == 0 && ((int)(ui->time * 2.5f) & 1) == 0)
            gfx_rect(sx + 30 + qw, sy + 6, 1.5f, 12, with_alpha(THEME_INK(th, graphite, LIGHT), a));
    } else {
        text_draw(sx + 28, sy + 16.5f, 0.6f, 0, with_alpha(THEME_INK(th, text_muted, TEXT_DIM), a), ALIGN_LEFT, L(S_SEARCH));
    }
    char cnt[24];
    snprintf(cnt, sizeof(cnt), "%d", n);
    text_draw(sx + sw - 12, sy + 16.5f, 0.54f, 0, with_alpha(THEME_INK(th, text_dim, TEXT_DIM), a), ALIGN_RIGHT, cnt);

    /* tangentbordet till vänster: 7 tangenter per rad */
    float kw = 22, kh = 20, kgap = 3, ky0 = sy + 34;
    for (int k = 0; k < SEARCH_KEY_COUNT; k++) {
        float kx = sx + (k % SEARCH_COLS) * (kw + kgap), ky = ky0 + (k / SEARCH_COLS) * (kh + kgap);
        int sel = k == ui->search_key;
        u32 tc;
        if (th->ink) {
            if (sel && ui->search_focus == 0) {
                gfx_round_rect(kx, ky, kw, kh, 4.5f, with_alpha(th->ink->graphite, a));
                tc = with_alpha(th->ink->text_inverse, a);
            } else if (sel) {
                gfx_round_rect(kx, ky, kw, kh, 4.5f, COLOR_ALPHA(th->ink->panel_deep, (int)(245 * a)));
                tc = with_alpha(th->ink->text, a);
            } else {
                gfx_round_rect(kx, ky, kw, kh, 4.5f, COLOR_ALPHA(th->ink->border, (int)(225 * a)));
                gfx_round_rect(kx + 1, ky + 1, kw - 2, kh - 2, 3.8f, COLOR_ALPHA(th->ink->panel, (int)(242 * a)));
                tc = with_alpha(th->ink->text, a);
            }
        } else {
            if (sel) {
                u32 kc = ui->search_focus == 0 ? with_alpha(LIGHT, 0.95f * a) : RGBA(255, 255, 255, (int)(55 * a));
                gfx_round_rect(kx, ky, kw, kh, 4.5f, kc);
            } else {
                gfx_round_rect(kx, ky, kw, kh, 4.5f, RGBA(255, 255, 255, (int)(16 * a)));
            }
            tc = (sel && ui->search_focus == 0) ? with_alpha(DARK, a) : with_alpha(TEXT_MAIN, a);
        }
        char ch = SEARCH_KEYS[k];
        if (ch == ' ') {
            gfx_rect(kx + kw / 2 - 4, ky + kh - 5.5f, 8, 1.6f, tc);
        } else if (ch == '<') {
            float tri[6] = { kx + kw / 2 - 4, ky + kh / 2, kx + kw / 2 + 2.5f, ky + kh / 2 - 3.5f,
                             kx + kw / 2 + 2.5f, ky + kh / 2 + 3.5f };
            gfx_fan(kx + kw / 2, ky + kh / 2, tri, 3, tc);
        } else {
            char s[2] = { ch, 0 };
            text_draw(kx + kw / 2, ky + 13.5f, 0.5f, sel, tc, ALIGN_CENTER, s);
        }
    }

    /* träffarna till höger om tangentbordet */
    float rx = sx + SEARCH_COLS * (kw + kgap) + 9, rw = sx + sw - rx, ry0 = ky0, rh = 31;
    u32 list_main = ink_over_art(th, th->ink ? th->ink->text : TEXT_MAIN, TEXT_MAIN, art_a);
    u32 list_dim = ink_over_art(th, th->ink ? th->ink->text_dim : TEXT_DIM, TEXT_DIM, art_a);
    if (!n) {
        text_draw(rx + rw / 2, ry0 + 60, 0.72f, 0, with_alpha(list_dim, a), ALIGN_CENTER, L(S_NO_RESULTS));
    } else {
        gfx_clip((int)rx - 2, (int)ry0 - 2, (int)rw + 4, (int)(SEARCH_ROWS * rh + 2));
        int first = (int)floorf(ui->search_scroll), last = first + SEARCH_ROWS;
        for (int r = first; r <= last && r < n; r++) {
            if (r < 0)
                continue;
            float y = ry0 + (r - ui->search_scroll) * rh;
            int sel = ui->search_focus == 1 && r == ui->search_sel;
            if (sel)
                theme_sel_box(th, rx, y, rw, rh - 3, 7, with_alpha(LIGHT, 0.94f * a));
            int idx = hits[r];
            const Entry *e = &ui->list->items[idx];
            IconSlot *slot = loader_icon(idx);
            float ix = rx + 6, iy = y + 3.5f, iw = 38, ih = 21;
            if (slot && slot->state == SLOT_READY)
                gfx_tex(slot->tex, ix, iy, iw, ih, RGBA(255, 255, 255, (int)(255 * a)));
            else
                gfx_round_rect(ix, iy, iw, ih, 3,
                               th->ink ? COLOR_ALPHA(th->ink->panel_alt, (int)(235 * a)) : RGBA(32, 34, 42, (int)(220 * a)));

            u32 main = sel ? with_alpha(THEME_INK(th, sel_ink, DARK), a) : with_alpha(list_main, a);
            u32 dim = sel ? (th->ink ? with_alpha(th->ink->text_dim, a) : RGBA(70, 72, 80, (int)(255 * a)))
                          : with_alpha(list_dim, a);
            char title[120];
            text_ellipsis(title, sizeof(title), e->title, 0.6f, rw - 110);
            text_draw(ix + iw + 10, y + 18.5f, 0.6f, 0, main, ALIGN_LEFT, title);
            text_draw(rx + rw - 10, y + 18.0f, 0.48f, 1, dim, ALIGN_RIGHT, type_name(e->type));
        }
        gfx_clip_reset();
    }

    draw_select_back(ui, MARGIN, 246, !th->ink || art_a > 0.3f, a);
}

static void draw_toast(Ui *ui)
{
    if (ui->toast_time <= 0)
        return;
    const Theme *th = theme_cur();
    float a = clampf(ui->toast_time / 0.4f, 0, 1) * clampf((2.8f - ui->toast_time) / 0.2f, 0, 1);
    float w = text_width(0.62f, 0, ui->toast) + 32;
    float x = (SCREEN_W - w) / 2, y = 132;
    gfx_glow(x, y, w, 28, 10, RGBA(0, 0, 0, (int)(150 * a)));
    gfx_round_rect(x, y, w, 28, 7,
                   COLOR_ALPHA(THEME_INK(th, graphite, RGBA(40, 41, 47, 255)), (int)(245 * a)));
    text_draw(SCREEN_W / 2, y + 18.5f, 0.62f, 0,
              COLOR_ALPHA(THEME_INK(th, text_inverse, RGBA(240, 240, 245, 255)), (int)(255 * a)),
              ALIGN_CENTER, ui->toast);
}

/* Kategoriväljaren: kategorimappens bg.png över hela skärmen, listan till vänster */
static void draw_categories(Ui *ui)
{
    const Theme *th = theme_cur();
    float a = smooth(ui->cat_anim);
    if (a <= 0)
        return;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(8, 8, 12, (int)(200 * a)));
    if (ui->cat_bg && ui->cat_bg_for == ui->cat_sel)
        gfx_tex(ui->cat_bg, 0, 0, SCREEN_W, SCREEN_H, RGBA(205, 205, 210, (int)(255 * a * smooth(ui->cat_bg_fade))));
    gfx_gradient_h(0, 0, 300, SCREEN_H, RGBA(0, 0, 0, (int)(170 * a)), RGBA(0, 0, 0, 0));

    float slide = (1 - a) * -16;
    u32 inv = THEME_INK(th, text_inverse, TEXT_MAIN);
    u32 inv_dim = th->ink ? th->ink->panel_alt : TEXT_DIM;
    text_draw(MARGIN + slide, 34, 0.95f, 0, with_alpha(inv, a), ALIGN_LEFT, L(S_CATEGORIES));
    float lw = 236;
    gfx_clip(0, 50, SCREEN_W, CAT_ROWS * CAT_ROW_H + 6);
    for (int r = 0; r < CAT_ROWS && ui->cat_scroll + r < s_npick; r++) {
        int i = (int)ui->cat_scroll + r, c = s_pick[i];
        float y = 54 + r * CAT_ROW_H;
        int selected = i == ui->cat_sel;
        if (selected)
            theme_sel_box(th, MARGIN - 6 + slide, y, lw, CAT_ROW_H - 3, 7, with_alpha(LIGHT, 0.95f * a));
        u32 main = with_alpha(selected ? THEME_INK(th, sel_ink, DARK) : inv, a);
        char name[48], num[12];
        text_ellipsis(name, sizeof(name), cat_title(c), 0.62f, lw - 60);
        snprintf(num, sizeof(num), "%d", s_cat_n[ui->tab][c]);
        text_draw(MARGIN + 4 + slide, y + 16.5f, 0.62f, 0, main, ALIGN_LEFT, name);
        u32 cnt_col = selected ? (th->ink ? with_alpha(th->ink->text_dim, a) : RGBA(70, 72, 80, (int)(255 * a)))
                               : with_alpha(inv_dim, a);
        text_draw(MARGIN - 16 + lw + slide, y + 16.5f, 0.52f, 0, cnt_col, ALIGN_RIGHT, num);
    }
    gfx_clip_reset();
    draw_select_back(ui, MARGIN, 246, 1, a);
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
    case CC_STANDBY: /* [medhatk] Standby Screen clock glyph (circle + 12/3 o'clock hands) */
        gfx_circle(cx, cy, 8.5f, c);
        gfx_circle(cx, cy, 6.5f, bg);
        gfx_round_rect(cx - 0.9f, cy - 5.0f, 1.8f, 5.9f, 0.9f, c);
        gfx_round_rect(cx - 0.9f, cy - 0.9f, 4.9f, 1.8f, 0.9f, c);
        break;
    case CC_USB:
        ui_draw_mask_icon(UI_ICON_CC_USB, cx, cy, c);
        break;
    case CC_FILES: /* mapp */
        gfx_round_rect(cx - 9, cy - 7.5f, 8, 5, 1.5f, c);
        gfx_round_rect(cx - 9, cy - 5, 18, 12, 2, c);
        break;
    case CC_SCREENSHOT: /* kamera */
        gfx_round_rect(cx - 4, cy - 8, 8, 4, 1.5f, c);
        gfx_round_rect(cx - 9.5f, cy - 5.5f, 19, 13, 2.5f, c);
        gfx_circle(cx, cy + 1, 4.2f, bg);
        gfx_circle(cx, cy + 1, 2.4f, c);
        break;
    case CC_SETTINGS: /* kugghjul */
        ui_draw_mask_icon(UI_ICON_CC_GEAR, cx, cy, c);
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

/* Panelens färg: temats (som inställningarna, lite mörkare för texten) – grå med egen bakgrundsbild */
static void cc_colors(u32 *top, u32 *bottom)
{
    const Theme *t = theme_cur();
    if (t->ink) {
        *top = t->ink->graphite_soft;
        *bottom = t->ink->graphite;
        return;
    }
    if (g_wallpaper[0]) {
        *top = RGBA(48, 49, 55, 255);
        *bottom = RGBA(26, 27, 31, 255);
        return;
    }
    *top = mix(t->stop, RGBA(0, 0, 0, 255), 0.25f);
    *bottom = mix(t->sbottom, RGBA(0, 0, 0, 255), 0.1f);
}

/* "Spelas nu" ovanför panelen, som musikkortet i PS5:s Control Center */
static void draw_cc_music(Ui *ui, float x, float y, float a, u32 bg)
{
    /* bara medan musik spelar – pausas den tonas kortet ut */
    static float shown, last;
    float dt = clampf(ui->time - last, 0, 0.05f);
    last = ui->time;
    if (ui->cc_open && ui->cc_anim < 0.15f) /* panelen öppnas: inget kvar från förra gången */
        shown = music_state() == MUSIC_PLAYING;
    shown = clampf(shown + (music_state() == MUSIC_PLAYING ? 6 : -6) * dt, 0, 1);
    int cur = music_current();
    const Track *t = cur >= 0 ? music_track(cur) : NULL;
    if (!t || shown <= 0)
        return;
    const Theme *th = theme_cur();
    a *= smooth(shown);
    const float w = 206, h = 46, as = 34;
    static char title[80], artist[64];
    static int text_for = -2, text_gen = -1;
    if (text_for != cur || text_gen != music_generation()) {
        text_ellipsis(title, sizeof(title), t->title, 0.56f, w - as - 44);
        text_ellipsis(artist, sizeof(artist), t->artist[0] ? t->artist : L(S_UNKNOWN_ARTIST), 0.46f, w - as - 44);
        text_for = cur;
        text_gen = music_generation();
    }
    gfx_glow(x, y + 2, w, h, 8, RGBA(0, 0, 0, (int)(120 * a)));
    gfx_round_rect(x, y, w, h, 12, with_alpha(bg, 0.96f * a));
    float ax = x + 6, ay = y + 6;
    gfx_round_rect(ax, ay, as, as, 7,
                   th->ink ? COLOR_ALPHA(th->ink->graphite, (int)(255 * a)) : RGBA(70, 72, 84, (int)(255 * a)));
    u32 inv = THEME_INK(th, text_inverse, LIGHT);
    draw_note(ax + as / 2, ay + as / 2, 16, with_alpha(inv, 0.9f * a));
    if (ui->cover && ui->cover_for == cur)
        gfx_tex(ui->cover, ax, ay, as, as, RGBA(255, 255, 255, (int)(255 * a * smooth(ui->cover_fade))));
    text_draw(ax + as + 9, y + 20, 0.56f, 0, with_alpha(THEME_INK(th, text_inverse, TEXT_MAIN), a), ALIGN_LEFT, title);
    text_draw(ax + as + 9, y + 35, 0.46f, 0,
              th->ink ? COLOR_ALPHA(th->ink->panel_alt, (int)(220 * a)) : with_alpha(TEXT_DIM, a), ALIGN_LEFT, artist);
    draw_play_glyph(x + w - 18, y + h / 2, 6, music_state() == MUSIC_PLAYING, with_alpha(inv, a));
}

/* Control Center: en panel som glider upp nerifrån med en rad ikoner, som PS5:s. Den vita
 * markeringen glider mellan ikonerna och namnet på valt alternativ står ovanför. */
static void draw_cc(Ui *ui)
{
    const Theme *th = theme_cur();
    float t = ui->cc_anim;
    if (t <= 0)
        return;
    float e = 1 - (1 - t) * (1 - t) * (1 - t); /* mjuk inbromsning */
    u32 top, bottom;
    cc_colors(&top, &bottom);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(110 * e)));

    /* [medhatk] Narrow Control Center icon stride from 48 to 44 px when CC_COUNT > 9 so all 10 icons fit comfortably */
    const float ph = 84, step = CC_COUNT > 9 ? 44 : 48;
    float py = SCREEN_H - ph * e;
    draw_cc_music(ui, MARGIN, py - 56, e, top);
    gfx_glow(0, py, SCREEN_W, ph + 20, 12, RGBA(0, 0, 0, (int)(150 * e)));
    gfx_round_rect(0, py, SCREEN_W, ph + 20, 16, with_alpha(top, 0.97f * e));
    gfx_gradient_v(0, py + 16, SCREEN_W, ph - 16, with_alpha(top, 0.97f * e), with_alpha(bottom, 0.97f * e));
    gfx_rect(16, py, SCREEN_W - 32, 1,
             th->ink ? COLOR_ALPHA(th->ink->border, (int)(110 * e)) : RGBA(255, 255, 255, (int)(22 * e))); /* svag ljus kant upptill */

    float x0 = SCREEN_W / 2 - (CC_COUNT - 1) * step / 2, cy = py + 52;
    float hx = x0 + ui->cc_pos * step;
    gfx_glow(hx - 20, cy - 18, 40, 40, 6, RGBA(0, 0, 0, (int)(90 * e)));
    u32 sel_bg = THEME_INK(th, sel_fill, LIGHT);
    u32 sel_fg = THEME_INK(th, sel_ink, DARK);
    u32 idle_fg = THEME_INK(th, text_inverse, LIGHT);
    gfx_round_rect(hx - 20, cy - 20, 40, 40, 10, with_alpha(sel_bg, e));

    u32 hole = mix(top, bottom, 0.6f); /* panelens färg vid ikonerna – för "hålen" i ikonerna */
    for (int k = 0; k < CC_COUNT; k++) {
        float f = clampf(1 - fabsf(k - ui->cc_pos), 0, 1); /* hur mycket markeringen täcker ikonen */
        float ia = smooth(t * 1.7f - k * 0.08f);           /* ikonerna tonas in en i taget */
        float iy = cy + (1 - ia) * 10;
        draw_cc_glyph(k, x0 + k * step, iy, with_alpha(mix(idle_fg, sel_fg, f), ia * e),
                      with_alpha(mix(hole, sel_bg, f), ia * e));
    }

    /* namnet ovanför markeringen */
    const char *label = ui->cc_confirm ? L(S_CONFIRM) : cc_label(ui->cc_sel);
    float la = smooth(ui->cc_label) * e;
    float lw = text_width(0.6f, 0, label);
    float lx = clampf(x0 + ui->cc_sel * step, MARGIN + lw / 2, SCREEN_W - MARGIN - lw / 2);
    text_draw(lx, py + 22 + (1 - la) * 4, 0.6f, 0,
              with_alpha(THEME_INK(th, text_inverse, TEXT_MAIN), la), ALIGN_CENTER, label);
}

static void draw_empty(Ui *ui)
{
    const Theme *th = theme_cur();
    float art_a = ui_art_alpha(ui);
    float a = ui->intro * smooth(ui->tab_anim);
    const char *title = ui->tab == TAB_APPS ? L(S_NO_APPS) : L(S_NO_GAMES);
    const char *hint = on_root(L(ui->tab == TAB_APPS ? S_HINT_APPS : S_HINT_GAMES));
    text_draw(MARGIN, 120, 0.9f, 0,
              with_alpha(ink_over_art(th, th->ink ? th->ink->text : TEXT_MAIN, TEXT_MAIN, art_a), a), ALIGN_LEFT, title);
    text_draw(MARGIN, 142, 0.62f, 0,
              with_alpha(ink_over_art(th, th->ink ? th->ink->text_dim : TEXT_DIM, TEXT_DIM, art_a), a), ALIGN_LEFT, hint);
}

void ui_draw(Ui *ui)
{
    const Theme *th = theme_cur();
    gfx_begin(THEME_INK(th, panel_alt, CLEAR_COLOR));
    draw_background(ui);
    /* Dämpar bakgrundsbild i Bibliotek/Musik för läsbarhet ovanpå rutnät/låtlista. */
    if (ui->tab == TAB_LIBRARY && (g_wallpaper[0] || ui->bg_cur || ui->bg_old))
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(9, 13, 19, g_wallpaper[0] && !ui->bg_cur ? 115 : 85));
    else if (ui->tab == TAB_MUSIC && g_wallpaper[0])
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(9, 13, 19, 115));
    else if ((ui->tab == TAB_PICTURES || ui->tab == TAB_VIDEO || ui->tab == TAB_SEARCH) && (!th->ink || g_wallpaper[0]))
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(8, 8, 12, 150));
    draw_top_bar(ui);
    if (ui->tab == TAB_GAMES || ui->tab == TAB_APPS || ui->tab == TAB_LIBRARY || ui->tab == TAB_MUSIC)
        ui_draw_footer_bar(ui->intro * (ui->launch < 0 ? 1.0f : 1.0f - smooth(ui->launch * 2.0f)));
    if (ui->tab == TAB_MEDIA) {
        draw_media_row(ui);
    } else if (ui->tab == TAB_MUSIC) {
        draw_music(ui);
    } else if (ui->tab == TAB_PICTURES) {
        draw_pictures(ui);
    } else if (ui->tab == TAB_VIDEO) {
        draw_videos(ui);
    } else if (ui->tab == TAB_SEARCH) {
        draw_search(ui);
    } else if (view(ui)->count && ui->tab == TAB_LIBRARY) {
        draw_library(ui);
    } else if (view(ui)->count) {
        if (ui->tab == TAB_GAMES || ui->tab == TAB_APPS)
            draw_games_redesign(ui);
        else {
            draw_row(ui);
            draw_details(ui);
        }
    } else {
        draw_empty(ui);
    }
    draw_hints(ui);
    if (ui->tab == TAB_PICTURES && ui->pic_viewing)
        draw_pic_fullscreen(ui);
    if (ui->tab == TAB_VIDEO && ui->vid_viewing)
        draw_vid_fullscreen(ui);
    if (ui->tab == TAB_GAMES || ui->tab == TAB_APPS || ui->tab == TAB_LIBRARY)
        draw_menu(ui);
    draw_categories(ui);
    draw_cc(ui);
    draw_toast(ui);
    osd_draw();

    /* in- och uttoning från/till svart */
    float black = 1 - ui->intro;
    if (ui->launch > 0)
        black = fmaxf(black, clampf((ui->launch - 0.35f) / 0.65f, 0, 1));
    if (ui->cc_leave > 0) /* Control Center: ut till svart innan USB, filer, viloläge … */
        black = fmaxf(black, smooth(ui->cc_leave));
    if (black > 0)
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(255 * black)));
    gfx_end();
}

/* [medhatk] Initialize/reset all Standby Screen state before entering standby_mode() */
void ui_standby_open(Ui *ui)
{
    ui->sb_time = 0;
    ui->sb_leave = 0;
    ui->sb_power_open = 0;
    ui->sb_power_sel = 0;
    ui->sb_confirm = 0;
    ui->sb_confirm_time = 0;
    ui->sb_power_anim = 0;
    ui->sb_step_dir = 0;
    ui->sb_step_time = 0;
}

/* [medhatk] Process input & animations in Standby Screen:
 * - Only HOME unlocks (SB_UNLOCK); Triangle toggles the Power panel (Sleep / Shut down).
 * - Confirm plays/pauses music; Left/Right skips tracks when music is loaded.
 * - Shut down requires a second confirm press within 3s to avoid accidental power-off. */
int ui_standby_update(Ui *ui, const UiInput *in, float dt)
{
    ui->time += dt;
    ui->sb_time += dt;
    ui->sb_power_anim = clampf(ui->sb_power_anim + (ui->sb_power_open ? 7.0f : -8.0f) * dt, 0, 1);
    if (ui->sb_step_time > 0) {
        ui->sb_step_time -= dt;
        if (ui->sb_step_time <= 0)
            ui->sb_step_dir = 0;
    }
    media_poll(ui);
    update_cover(ui, dt);
    if (music_needs_cjk())
        font_ensure_cjk();

    if (ui->sb_confirm) {
        ui->sb_confirm_time += dt;
        if (ui->sb_confirm_time >= 3.0f) {
            ui->sb_confirm = 0;
            ui->sb_confirm_time = 0;
        }
    }

    /* [medhatk] 0.3s entry guard absorbs any lingering button press from Control Center */
    if (ui->sb_time < 0.3f)
        return SB_NONE;

    if (in->pressed & PSP_CTRL_HOME) {
        sound_play(SND_SIDE_OUT);
        return SB_UNLOCK;
    }

    u32 ok = ui->confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
    u32 back = ui->confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;

    /* [medhatk] Power overlay active: navigate between Sleep (0) and Shut down (1) */
    if (ui->sb_power_open) {
        int move = step_input(ui, in, dt, 0);
        if (move) {
            ui->sb_power_sel ^= 1;
            ui->sb_confirm = 0;
            ui->sb_confirm_time = 0;
            sound_play(SND_MOVE);
        }
        if (in->pressed & (back | PSP_CTRL_TRIANGLE)) {
            ui->sb_power_open = 0;
            ui->sb_confirm = 0;
            ui->sb_confirm_time = 0;
            sound_play(SND_CLOSE);
        } else if (in->pressed & ok) {
            if (ui->sb_power_sel == 0) {
                sound_play(SND_SELECT);
                return SB_SLEEP;
            }
            if (!ui->sb_confirm) {
                ui->sb_confirm = 1;
                ui->sb_confirm_time = 0;
                sound_play(SND_MSG_TOAST);
            } else {
                sound_play(SND_SELECT);
                return SB_SHUTDOWN;
            }
        }
        return SB_NONE;
    }

    /* [medhatk] Main lock-screen shortcuts: Triangle = Power menu, OK = play/pause, Left/Right = track skip */
    if (in->pressed & PSP_CTRL_TRIANGLE) {
        ui->sb_power_open = 1;
        ui->sb_power_sel = 0;
        ui->sb_confirm = 0;
        ui->sb_confirm_time = 0;
        sound_play(SND_OPEN);
    } else if (in->pressed & ok) {
        if (music_current() >= 0 && music_state() != MUSIC_STOPPED) {
            music_toggle_pause();
            sound_play(SND_MUSIC_ACT);
        } else if (music_count()) {
            music_play(ui->music_sel);
            sound_play(SND_MUSIC_ACT);
        } else {
            sound_play(SND_ERROR);
        }
    } else if (music_current() >= 0) {
        if (in->pressed & PSP_CTRL_LEFT) {
            music_prev();
            ui->sb_step_dir = -1;
            ui->sb_step_time = 0.22f;
            sound_play(SND_MUSIC_ACT);
        } else if (in->pressed & PSP_CTRL_RIGHT) {
            music_next();
            ui->sb_step_dir = 1;
            ui->sb_step_time = 0.22f;
            sound_play(SND_MUSIC_ACT);
        }
    }
    return SB_NONE;
}

/* [medhatk] Render one Standby Screen frame using the active theme or custom wallpaper */
void ui_draw_standby(Ui *ui)
{
    const Theme *th = theme_cur();
    float art_a = g_wallpaper[0] ? 1.0f : 0.0f;
    gfx_begin(THEME_INK(th, panel_alt, CLEAR_COLOR));
    ui_draw_calm_backdrop(th, ui->time * 0.3f, ui->accent, 1);

    u32 clk_col = ink_over_art(th, th->ink ? th->ink->text : TEXT_MAIN, TEXT_MAIN, art_a);
    u32 dim_col = ink_over_art(th, th->ink ? th->ink->text_dim : TEXT_DIM, TEXT_DIM, art_a);

    /* [medhatk] Battery percentage & icon in the top-right corner */
    float bx = SCREEN_W - 36.0f;
    int battery = scePowerIsBatteryExist();
    int charging = testrun_charging() >= 0 ? testrun_charging() : battery && scePowerIsBatteryCharging() > 0;
    int level = testrun_battery() >= 0 ? testrun_battery() : battery ? scePowerGetBatteryLifePercent() : -1;
    char bat_info[12] = "";
    if (level >= 0)
        snprintf(bat_info, sizeof(bat_info), "%d%%", level);
    float iw = bat_info[0] ? text_width(0.58f, 0, bat_info) + 5.0f : 0.0f;
    if (bat_info[0])
        text_draw(bx - 5.0f, 24.0f, 0.58f, 0, with_alpha(dim_col, 0.95f), ALIGN_RIGHT, bat_info);
    draw_battery(bx, 15.0f, level, charging, ui->time, 0.95f, iw, th, art_a);

    int cur = music_current(), mstate = music_state();
    const Track *tr = cur >= 0 ? music_track(cur) : NULL;
    int has_music = tr && mstate != MUSIC_STOPPED;

    /* [medhatk] Large centered digital clock (with blinking colon) + date; shifts slightly up when music card is shown */
    ScePspDateTime now;
    if (read_clock(&now)) {
        char hh[4], mm[4], date[20];
        snprintf(hh, sizeof(hh), "%02d", now.hour);
        snprintf(mm, sizeof(mm), "%02d", now.minute);
        const float clk_sz = 1.8f;
        float hw = text_width(clk_sz, 0, hh);
        float cw = text_width(clk_sz, 0, ":");
        float mw = text_width(clk_sz, 0, mm);
        float tx = SCREEN_W * 0.5f - (hw + cw + mw) * 0.5f;
        float cy = has_music ? 60.0f : 96.0f;
        text_draw(tx, cy, clk_sz, 0, clk_col, ALIGN_LEFT, hh);
        text_draw(tx + hw, cy, clk_sz, 0, with_alpha(clk_col, (now.second & 1) ? 0.45f : 1.0f), ALIGN_LEFT, ":");
        text_draw(tx + hw + cw, cy, clk_sz, 0, clk_col, ALIGN_LEFT, mm);

        snprintf(date, sizeof(date), "%02d/%02d/%04d", now.day, now.month, now.year);
        text_draw(SCREEN_W * 0.5f, cy + 21.0f, 0.60f, 0, dim_col, ALIGN_CENTER, date);
    }

    /* [medhatk] Compact launcher-style Now Playing card (168x140) centered under the clock when playing/paused */
    u32 card_top, card_bot;
    cc_colors(&card_top, &card_bot);
    if (has_music) {
        const float pw = 168.0f, ph = 140.0f;
        const float px = (SCREEN_W - pw) * 0.5f, py = 94.0f;
        u32 border = th->ink      ? COLOR_ALPHA(th->ink->border, 235)
                   : g_theme == 0 ? RGBA(44, 55, 74, 210)
                                  : with_alpha(th->accent, 0.35f);
        u32 panel_bg = th->ink      ? COLOR_ALPHA(th->ink->panel, 242)
                     : g_theme == 0 ? RGBA(18, 24, 34, 238)
                                    : with_alpha(th->card, 0.90f);
        gfx_glow(px, py + 2.0f, pw, ph, 8.0f, RGBA(0, 0, 0, 120));
        gfx_round_rect(px, py, pw, ph, 10.0f, border);
        gfx_round_rect(px + 1.0f, py + 1.0f, pw - 2.0f, ph - 2.0f, 9.2f, panel_bg);

        /* [medhatk] 44x44 album cover thumbnail with note-icon fallback */
        float as = 44.0f, ax = px + (pw - as) * 0.5f, ay = py + 9.0f;
        unsigned h = (unsigned)cur * 2654435761u;
        int r0 = 60 + (h >> 8) % 120, g0 = 60 + (h >> 16) % 110, b0 = 90 + (h >> 24) % 140;
        gfx_glow(ax, ay + 2.0f, as, as, 6.0f, RGBA(0, 0, 0, 110));
        gfx_round_rect(ax, ay, as, as, 7.0f, RGBA(r0, g0, b0, 255));
        gfx_gradient_v(ax + 3.0f, ay + as * 0.5f, as - 6.0f, as * 0.5f - 3.0f,
                       RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 65));
        draw_note(ax + as * 0.5f, ay + as * 0.5f, 20.0f, RGBA(255, 255, 255, 230));
        if (ui->cover && ui->cover_for == cur)
            gfx_tex(ui->cover, ax, ay, as, as, RGBA(255, 255, 255, (int)(255 * smooth(ui->cover_fade))));

        /* [medhatk] Cache ellipsized track title & artist until track, music list, or language changes */
        static char title[96], artist[64];
        static int text_for = -2, text_gen = -1;
        static const char *text_lang;
        if (text_for != cur || text_gen != music_generation() || text_lang != L(S_UNKNOWN_ARTIST)) {
            text_ellipsis(title, sizeof(title), tr->title, 0.54f, pw - 20.0f);
            text_ellipsis(artist, sizeof(artist), tr->artist[0] ? tr->artist : L(S_UNKNOWN_ARTIST), 0.43f,
                          pw - 20.0f);
            text_for = cur;
            text_gen = music_generation();
            text_lang = L(S_UNKNOWN_ARTIST);
        }
        text_draw(px + pw * 0.5f, ay + as + 13.0f, 0.54f, 0, THEME_INK(th, text, TEXT_MAIN), ALIGN_CENTER, title);
        text_draw(px + pw * 0.5f, ay + as + 25.0f, 0.43f, 0,
                  th->ink ? COLOR_ALPHA(th->ink->text_dim, 245) : RGBA(148, 163, 184, 235), ALIGN_CENTER, artist);

        /* [medhatk] Playback progress bar, elapsed/duration timestamps, and [prev] [play/pause] [next] controls */
        float el = music_elapsed(), du = music_duration();
        float pbw = pw - 24.0f, pbx = px + 12.0f, pby = py + ph - 46.0f;
        gfx_round_rect(pbx, pby, pbw, 2.5f, 1.25f,
                       th->ink ? COLOR_ALPHA(th->ink->panel_deep, 240) : RGBA(44, 56, 74, 220));
        if (du > 0.0f) {
            float fw = fmaxf(2.5f, pbw * clampf(el / du, 0.0f, 1.0f));
            u32 bar = THEME_INK(th, graphite, th->accent);
            gfx_round_rect(pbx, pby, fw, 2.5f, 1.25f, with_alpha(bar, 0.96f));
            gfx_circle(pbx + fw, pby + 1.25f, 2.5f, with_alpha(bar, 1.0f));
        }
        char e[12], d[12];
        format_time(e, sizeof(e), el);
        format_time(d, sizeof(d), du);
        u32 ts_col = th->ink ? COLOR_ALPHA(th->ink->text_dim, 240) : RGBA(140, 152, 172, 235);
        text_draw(pbx, pby + 11.0f, 0.42f, 0, ts_col, ALIGN_LEFT, e);
        if (du > 0.0f)
            text_draw(pbx + pbw, pby + 11.0f, 0.42f, 0, ts_col, ALIGN_RIGHT, d);

        float ctx = px + pw * 0.5f, cty = py + ph - 17.0f;
        draw_transport_step(ctx - 32.0f, cty, -1, ui->sb_step_time > 0 && ui->sb_step_dir < 0, 1.0f);
        gfx_circle(ctx, cty, 9.5f, with_alpha(THEME_INK(th, graphite, LIGHT), 0.96f));
        draw_play_glyph(ctx, cty, 5.6f, mstate == MUSIC_PLAYING, THEME_INK(th, text_inverse, DARK));
        draw_transport_step(ctx + 32.0f, cty, 1, ui->sb_step_time > 0 && ui->sb_step_dir > 0, 1.0f);
    }

    /* [medhatk] Bottom hint bar: centered HOME unlock prompt + right-aligned Triangle Power shortcut */
    const char *pwr = L(S_POWER);
    float pw = text_width(0.52f, 0, pwr);
    float pwr_x = SCREEN_W - MARGIN - pw;
    float hy = 252.0f;
    theme_button(th, pwr_x - 10.5f, hy, 6.0f, BTN_TRIANGLE, art_a > 0.3f,
                 with_alpha(LIGHT, 0.88f), with_alpha(DARK, 0.95f));
    text_draw(pwr_x, hy + 4.0f, 0.52f, 0, with_alpha(dim_col, 0.95f), ALIGN_LEFT, pwr);

    const char *unlock = L(S_STANDBY_UNLOCK);
    float max_uw = (pwr_x - 28.0f - MARGIN) * 2.0f - 16.0f;
    if (max_uw < 80.0f)
        max_uw = 80.0f;
    float usz = text_fit(0.54f, 0.40f, 0, max_uw, unlock);
    float uw = text_width(usz, 0, unlock);
    float ux = SCREEN_W * 0.5f - (14.0f + uw) * 0.5f;
    if (ux + 14.0f + uw > pwr_x - 22.0f)
        ux = pwr_x - 22.0f - (14.0f + uw);
    if (ux < MARGIN)
        ux = MARGIN;
    /* [medhatk] PlayStation logo glyph from Figma representing the PSP HOME/PS button */
    static Texture *ps_tex;
    u32 hc = with_alpha(dim_col, 0.95f);
    draw_icon(ICON_PS_LOGO, &ps_tex, ux + 6.5f, hy, hc);
    text_draw(ux + 15.0f, hy + 4.0f, usz, 0, hc, ALIGN_LEFT, unlock);

    /* [medhatk] Slide-up Power overlay (Sleep / Shut down) when Triangle is toggled */
    if (ui->sb_power_anim > 0.01f) {
        float pa = smooth(ui->sb_power_anim);
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(95 * pa)));
        const float pan_w = 210.0f, pan_h = 76.0f;
        float pan_x = (SCREEN_W - pan_w) * 0.5f;
        float pan_y = 236.0f - pan_h + (1.0f - pa) * 10.0f;
        gfx_glow(pan_x, pan_y, pan_w, pan_h, 10.0f, RGBA(0, 0, 0, (int)(150 * pa)));
        gfx_round_rect(pan_x, pan_y, pan_w, pan_h, 14.0f, with_alpha(card_top, 0.97f * pa));
        gfx_ring(pan_x, pan_y, pan_w, pan_h, 14.0f,
                 th->ink ? COLOR_ALPHA(th->ink->border, (int)(90 * pa)) : RGBA(255, 255, 255, (int)(26 * pa)));

        float cx0 = pan_x + pan_w * 0.5f - 28.0f;
        float cx1 = pan_x + pan_w * 0.5f + 28.0f;
        float iy = pan_y + 49.0f;
        float sel_x = ui->sb_power_sel ? cx1 : cx0;
        u32 sel_bg = THEME_INK(th, sel_fill, LIGHT);
        u32 sel_fg = THEME_INK(th, sel_ink, DARK);
        u32 idle_fg = THEME_INK(th, text_inverse, LIGHT);
        gfx_round_rect(sel_x - 20.0f, iy - 20.0f, 40.0f, 40.0f, 10.0f, with_alpha(sel_bg, pa));

        int s0 = ui->sb_power_sel == 0, s1 = ui->sb_power_sel == 1;
        draw_cc_glyph(CC_SLEEP, cx0, iy, with_alpha(s0 ? sel_fg : idle_fg, pa),
                      with_alpha(s0 ? sel_bg : card_top, pa));
        draw_cc_glyph(CC_OFF, cx1, iy, with_alpha(s1 ? sel_fg : idle_fg, pa),
                      with_alpha(s1 ? sel_bg : card_top, pa));

        const char *lbl = ui->sb_confirm ? L(S_CONFIRM) : L(ui->sb_power_sel == 0 ? S_SLEEP : S_SHUTDOWN);
        float lsz = text_fit(0.56f, 0.40f, 0, pan_w - 20.0f, lbl);
        text_draw(pan_x + pan_w * 0.5f, pan_y + 20.0f, lsz, 0,
                  with_alpha(THEME_INK(th, text_inverse, TEXT_MAIN), pa), ALIGN_CENTER, lbl);
    }

    /* [medhatk] Keep volume & screenshot OSD functional on top of Standby Screen, then apply entry/exit fade */
    osd_draw();

    float black = 1.0f - clampf(ui->sb_time / 0.25f, 0.0f, 1.0f);
    if (ui->sb_leave > 0.0f)
        black = fmaxf(black, smooth(ui->sb_leave));
    if (black > 0.0f)
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(255 * black)));
    gfx_end();
}

void ui_draw_scanning(int done, const char *name, float time)
{
    static const float neutral[3] = { 120, 120, 120 };
    gfx_begin(CLEAR_COLOR);
    draw_spotlight(neutral, time);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, theme_cur()->ink && !g_wallpaper[0] ? 235 : 90)); /* lugnare bakgrund bakom loggan */

    /* loggan i mitten, en tunn list där en ljus bit glider fram och tillbaka, texten under */
    float cx = SCREEN_W / 2;
    Texture *lt = logo_texture(); /* [medhatk] Keep scan progress bar clear of taller stacked boot logos */
    float lh = lt ? (float)lt->h : 54.0f;
    float ly = lh > 80.0f ? 88.0f : 108.0f;
    logo_draw(cx, ly, RGBA(236, 236, 240, 255));
    float bw = 120, bx = cx - bw / 2, by = lh > 80.0f ? (ly + lh * 0.5f + 10.0f) : 146.0f, seg = 34;
    gfx_round_rect(bx, by, bw, 2, 1, RGBA(255, 255, 255, 38));
    float p = 0.5f - 0.5f * cosf(time * 2.4f);
    gfx_round_rect(bx + p * (bw - seg), by, seg, 2, 1, COLOR_ALPHA(theme_cur()->accent, 255));
    float ty = lh > 80.0f ? (by + 26.0f) : 172.0f;
    text_draw(cx, ty, 0.56f, 0, TEXT_DIM, ALIGN_CENTER, L(S_LOADING));
    char line[96], fit[96];
    snprintf(line, sizeof(line), "%d  ·  %.60s", done, name ? name : "");
    text_ellipsis(fit, sizeof(fit), line, 0.46f, 300);
    text_draw(cx, ty + 16.0f, 0.46f, 0, COLOR_ALPHA(TEXT_DIM, 150), ALIGN_CENTER, fit);

    /* tonas in från startskärmens svarta slut */
    float in = clampf(time / 0.4f, 0, 1);
    if (in < 1)
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(255 * (1 - in))));
    gfx_end();
}
