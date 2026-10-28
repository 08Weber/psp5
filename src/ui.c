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
#include "settings.h" /* SA_* för Control Center */
#include "boot.h"      /* psp5-loggan */

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

/* --- senast spelat och fästa spel ---
 * <data>/played.txt (senast spelat först) och pins.txt: en nyckel (entry_cover_key) per rad.
 * I minnet ligger de i s_meta, parallellt med spellistan. */

typedef struct {
    u32 played; /* högre = spelat senare, 0 = aldrig */
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
/* Sparar played.txt eller pins.txt ur s_meta. UMD-skivan sparas aldrig. */
enum { META_PLAYED, META_PINS };
static void meta_save(const EntryList *l, int which)
{
    int *idx = s_meta ? malloc((l->count ? l->count : 1) * sizeof(int)) : NULL;
    if (!idx)
        return;
    int n = 0;
    for (int i = 0; i < l->count; i++) {
        const Meta *m = &s_meta[i];
        if (l->items[i].type != ENTRY_UMD &&
            (which == META_PLAYED ? m->played > 0 : m->pinned))
            idx[n++] = i;
    }
    if (which == META_PLAYED) {
        qsort(idx, n, sizeof(int), cmp_played_desc);
        if (n > MAX_PLAYED)
            n = MAX_PLAYED;
    }
    keyfile_write(l, which == META_PLAYED ? "played.txt" : "pins.txt", idx, n);
    free(idx);
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
 * (som XMB). Lika: listans ordning (A–Ö). */
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

static void build_views(Ui *ui)
{
    collect_cats(ui->list);
    memset(s_cat_n, 0, sizeof(s_cat_n));
    for (int i = 0; i < ui->list->count; i++) {
        if (is_dupe(ui->list, i))
            continue;
        int c = cat_of(&ui->list->items[i]);
        s_cat_n[TAB_GAMES][c]++;
        s_cat_n[TAB_LIBRARY][c]++;
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
        LoaderView *v = &ui->views[TAB_GAMES];
        /* UMD-skivan syns i alla kategorier; alla spel och appar ligger i Games-raden som på PS5 */
        if (v->idx && (!ui->cat[TAB_GAMES] || c == ui->cat[TAB_GAMES] || e->type == ENTRY_UMD))
            v->idx[v->count++] = i;
        LoaderView *lib = &ui->views[TAB_LIBRARY];
        if (lib->idx && (!ui->cat[TAB_LIBRARY] || c == ui->cat[TAB_LIBRARY] || e->type == ENTRY_UMD))
            lib->idx[lib->count++] = i;
    }
    s_sort_list = ui->list;
    LoaderView *g = &ui->views[TAB_GAMES];
    if (g->idx) {
        qsort(g->idx, g->count, sizeof(int), cmp_row);
        int keep = 0; /* skivan och fästa spel står alltid kvar */
        while (keep < g->count && (ui->list->items[g->idx[keep]].type == ENTRY_UMD || meta(g->idx[keep])->pinned))
            keep++;
        if (g->count > keep + RECENT_GAMES)
            g->count = keep + RECENT_GAMES;
    }
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

static void switch_tab(Ui *ui, int tab)
{
    if (tab == ui->tab || tab < 0 || tab >= TAB_COUNT)
        return;
    if (tab == TAB_SEARCH) /* ○ i sökningen går tillbaka hit */
        ui->search_from = ui->tab;
    ui->tab = tab;
    ui->tab_anim = 0;
    sound_play(SND_SWIPE);
    ui->pos = ui->sel[tab] + 0.7f; /* raden glider in från höger */
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
    if (ui->tab == TAB_GAMES && view(ui)->count && ui->sel[TAB_GAMES] == view(ui)->count)
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
    ui->search_launch_idx = -1;
    ui->tab_anim = 1;
    ui->accent[0] = ui->accent[1] = ui->accent[2] = 120;
    ui->cat_bg_for = -1;
    collect_cats(list); /* last_load letar kategorierna med namn */
    meta_load(list);    /* fästa spel, senast spelat och egen ordning – före sorteringen */
    char keys[TAB_MUSIC][80] = { "" };
    int had_last = last_load(ui, keys);
    build_views(ui);
    if (!had_last || ui->tab == TAB_APPS)
        ui->tab = TAB_GAMES;
    char key[80];
    for (int t = 0; t < TAB_MUSIC; t++)
        for (int p = 0; keys[t][0] && p < ui->views[t].count; p++) {
            entry_cover_key(&list->items[ui->views[t].idx[p]], key, sizeof(key));
            if (!strcmp(key, keys[t])) {
                ui->sel[t] = p;
                break;
            }
        }
    ui->pos = (ui->tab >= TAB_MUSIC ? 0 : ui->sel[ui->tab]) + 1.5f; /* korten glider in vid start */
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
        if (added_front && (t == TAB_GAMES || t == TAB_LIBRARY) && ui->sel[t] == 0)
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

/* □-menyn för valt kort: Fäst (i Spel-raden) – i biblioteket även sorteringen */
enum { MI_PIN, MI_SORT };

static int menu_count(const Ui *ui)
{
    return ui->tab == TAB_LIBRARY ? 2 : 1;
}

static const char *menu_label(const Ui *ui, int item, char *buf, int size)
{
    if (item == MI_PIN)
        return L(meta(ui_selected(ui))->pinned ? S_UNPIN : S_PIN);
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

static void menu_choose(Ui *ui)
{
    int i = ui_selected(ui);
    switch (ui->menu_sel) {
    case MI_PIN:
        if (!s_meta) { /* inget minne för listan – då går det inte att fästa */
            sound_play(SND_ERROR);
            break;
        }
        meta(i)->pinned ^= 1;
        meta_save(ui->list, META_PINS);
        ui->menu_open = 0;
        resort(ui);
        sound_play(SND_TOGGLE);
        break;
    case MI_SORT: /* menyn står kvar – nästa tryck nästa sortering */
        toggle_sort(ui);
        break;
    }
}

void ui_free(Ui *ui)
{
    if (ui->list) {
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
        int next = (ui->album_sel + move + s_nalb + 1) % (s_nalb + 1);
        if (next != ui->album_sel) {
            ui->album_sel = next;
            sound_play(SND_MOVE);
        }
    } else if (move && hi > lo) {
        int next = lo + (ui->music_sel - lo + move + (hi - lo)) % (hi - lo);
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
            switch_tab(ui, TAB_MEDIA);
        }
        sound_play(SND_BACK);
        return;
    }
    if (!n)
        return;
    if (in->pressed & PSP_CTRL_TRIANGLE) {
        ui->music_view ^= 1;
        ui->album_sel = ui->music_album;
        sound_play(SND_TOGGLE);
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
        }
        ui->seek_dir = 0;
        ui->seeking = 0;
    }
    if (in->pressed & PSP_CTRL_SQUARE) { /* blanda – gäller från nästa låt */
        g_shuffle ^= 1;
        config_save(g_data_dir);
        ui_toast(ui, L(g_shuffle ? S_SHUFFLE_ON : S_SHUFFLE_OFF));
        sound_play(SND_TOGGLE);
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
        } else if (playing && ui->music_sel == music_current())
            music_toggle_pause();
        else
            music_play(ui->music_sel);
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

static PicItem *s_pics;
static int s_npics, s_pics_cap, s_pics_scanned, s_pics_gen;

static int cmp_pic(const void *a, const void *b)
{
    const PicItem *x = a, *y = b;
    int c = strcasecmp(x->sub, y->sub);
    return c ? c : strcasecmp(x->title, y->title);
}

static void add_pic(const char *path, const char *name, const char *folder, const char *dot)
{
    if (s_npics >= MAX_PICS)
        return;
    if (s_npics >= s_pics_cap) {
        int ncap = s_pics_cap ? s_pics_cap * 2 : 32;
        PicItem *p = realloc(s_pics, ncap * sizeof(*s_pics));
        if (!p)
            return;
        s_pics = p;
        s_pics_cap = ncap;
    }
    PicItem *it = &s_pics[s_npics++];
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
    while (sceIoDread(d, &ent) > 0 && s_npics < MAX_PICS) {
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

static void ensure_pics(void)
{
    if (osd_take_screenshot_done())
        s_pics_scanned = 0;
    if (s_pics_scanned)
        return;
    s_pics_scanned = 1;
    s_npics = 0;
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
    if (s_npics > 1)
        qsort(s_pics, s_npics, sizeof(*s_pics), cmp_pic);
    s_pics_gen++;
}

static void set_pic_wallpaper(Ui *ui, const char *path)
{
    if (strlen(path) >= sizeof(g_wallpaper)) {
        sound_play(SND_ERROR);
        return;
    }
    snprintf(g_wallpaper, sizeof(g_wallpaper), "%s", path);
    config_save(g_data_dir);
    ui_toast(ui, L(S_WALLPAPER_SET));
    sound_play(SND_SELECT);
}

static void update_pictures(Ui *ui, const UiInput *in, float dt)
{
    ensure_pics();
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
            sound_play(SND_MOVE);
        }
        if (in->pressed & PSP_CTRL_SQUARE)
            set_pic_wallpaper(ui, s_pics[ui->pic_sel].path);
        else if (in->pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_CROSS)) {
            ui->pic_viewing = 0;
            sound_play(SND_BACK);
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
            int next = (ui->pic_sel + move / 2 + n) % n;
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
                sound_play(SND_SELECT);
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
typedef struct {
    Entry entry;
    char title[72];
    char sub[48];
} VidItem;

static VidItem *s_vids;
static int s_nvids, s_vids_cap, s_vids_scanned;

static void add_vid_item(const Entry *e, const char *title, const char *sub)
{
    if (s_nvids >= MAX_VIDS)
        return;
    if (s_nvids >= s_vids_cap) {
        int ncap = s_vids_cap ? s_vids_cap * 2 : 32;
        VidItem *p = realloc(s_vids, ncap * sizeof(*s_vids));
        if (!p)
            return;
        s_vids = p;
        s_vids_cap = ncap;
    }
    VidItem *it = &s_vids[s_nvids++];
    it->entry = *e;
    snprintf(it->title, sizeof(it->title), "%s", title);
    snprintf(it->sub, sizeof(it->sub), "%s", sub);
}

static void scan_vid_dir(const char *dir, const char *folder, int depth)
{
    SceUID d = sceIoDopen(dir);
    if (d < 0)
        return;
    SceIoDirent ent;
    char path[160];
    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(d, &ent) > 0 && s_nvids < MAX_VIDS) {
        if (ent.d_name[0] != '.' && snprintf(path, sizeof(path), "%s/%s", dir, ent.d_name) < (int)sizeof(path)) {
            if (FIO_S_ISDIR(ent.d_stat.st_mode)) {
                if (depth < 2)
                    scan_vid_dir(path, ent.d_name, depth + 1);
            } else if (ent.d_stat.st_size > 2048) {
                const char *dot = strrchr(ent.d_name, '.');
                if (dot && dot != ent.d_name && (!strcasecmp(dot, ".pmf") || !strcasecmp(dot, ".mps"))) {
                    Entry e;
                    memset(&e, 0, sizeof(e));
                    e.type = ENTRY_HOMEBREW;
                    snprintf(e.path, sizeof(e.path), "%s", path);
                    e.vid_offset = 0;
                    e.vid_size = (u32)ent.d_stat.st_size;
                    int stem = (int)(dot - ent.d_name);
                    if (stem >= (int)sizeof(e.title))
                        stem = sizeof(e.title) - 1;
                    snprintf(e.title, sizeof(e.title), "%.*s", stem, ent.d_name);
                    char sub[48];
                    snprintf(sub, sizeof(sub), "%s  ·  PMF", folder && folder[0] ? folder : "VIDEO");
                    add_vid_item(&e, e.title, sub);
                }
            }
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(d);
}

static void ensure_videos(const Ui *ui)
{
    if (s_vids_scanned)
        return;
    s_vids_scanned = 1;
    s_nvids = 0;
    static const char *const dirs[] = { "VIDEO", "PSP/VIDEO" };
    for (int d = 0; d < 2; d++) {
        if (d == 0 && sys_ms_is_ef())
            continue;
        const char *dev = d == 0 ? "ms0:" : "ef0:";
        for (int k = 0; k < 2; k++) {
            char root[64];
            snprintf(root, sizeof(root), "%s/%s", dev, dirs[k]);
            scan_vid_dir(root, "VIDEO", 0);
        }
    }
    /* Lägg även till spelens egna ICON1.PMF-videor så att videospelaren kan spela dem */
    if (ui && ui->list) {
        for (int i = 0; i < ui->list->count && s_nvids < MAX_VIDS; i++) {
            const Entry *e = &ui->list->items[i];
            if (!is_dupe(ui->list, i) && e->vid_size > 2048) {
                char sub[48];
                snprintf(sub, sizeof(sub), "%s  ·  PMF", e->game_id[0] ? e->game_id : "ICON1");
                add_vid_item(e, e->title, sub);
            }
        }
    }
}

static void update_videos(Ui *ui, const UiInput *in, float dt)
{
    ensure_videos(ui);
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
            sound_play(SND_MOVE);
        }
        if (in->pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_CROSS)) {
            ui->vid_viewing = 0;
            sound_play(SND_BACK);
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
            int next = (ui->vid_sel + move / 2 + n) % n;
            if (next != ui->vid_sel) {
                ui->vid_sel = next;
                ui->vid_still = 0;
                sound_play(SND_MOVE);
            }
        }
        u32 confirm = (ui->confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS) | PSP_CTRL_START;
        if (ui->intro >= 1 && (in->pressed & confirm)) {
            ui->vid_viewing = 1;
            ui->vid_still = 1.0f;
            sound_play(SND_SELECT);
        }
    }

    if (ui->vid_sel < ui->vid_target)
        ui->vid_target = ui->vid_sel;
    if (ui->vid_sel > ui->vid_target + MROWS - 1)
        ui->vid_target = ui->vid_sel - (MROWS - 1);
    ui->vid_scroll = approach(ui->vid_scroll, ui->vid_target, 14.0f, dt);
    ui->vid_still += dt;
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
            int next = (ui->search_sel + move / 2 + n) % n;
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
            }
        } else if (in->pressed & confirm) {
            if (ui->search_focus == 0) {
                char ch = SEARCH_KEYS[ui->search_key];
                int len = strlen(ui->search_query);
                if (ch == '<') {
                    if (len > 0) {
                        ui->search_query[len - 1] = 0;
                        sound_play(SND_BACK);
                    }
                } else if (len + 1 < (int)sizeof(ui->search_query)) {
                    ui->search_query[len] = ch;
                    ui->search_query[len + 1] = 0;
                    sound_play(SND_SELECT);
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
        int next = (ui->cat_sel + move / 2 + s_npick) % s_npick;
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
            ui->pos = ui->sel[ui->tab] + 0.7f;
            ui->tab_anim = 0;
        }
        sound_play(SND_SELECT);
    } else if (in->pressed & PSP_CTRL_SELECT) {
        ui->cat_open = 0;
        ui_open_cc(ui); /* andra trycket på SELECT öppnar Control Center */
    } else if (in->pressed & back) {
        ui->cat_open = 0;
        sound_play(SND_BACK);
    }
}

/* --- Control Center (HOME eller SELECT) --- */

enum { CC_MUSIC, CC_SCREENSHOT, CC_SLEEP, CC_USB, CC_FILES, CC_SETTINGS, CC_XMB, CC_RESTART, CC_OFF, CC_COUNT };

static const char *cc_label(int item)
{
    switch (item) {
    case CC_MUSIC:      return L(music_state() == MUSIC_PLAYING ? S_PAUSE : S_PLAY);
    case CC_SCREENSHOT: return L(S_SCREENSHOT);
    case CC_SLEEP:      return L(S_SLEEP);
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
    sound_play(SND_OPEN);
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
        sound_play(SND_BACK);
        return UI_NONE;
    }
    if (!(in->pressed & ok))
        return UI_NONE;
    static const int actions[CC_COUNT] = { 0, 0, SA_SUSPEND, SA_USB, SA_FILES, 0, SA_XMB, SA_RESTART, SA_SHUTDOWN };
    switch (ui->cc_sel) {
    case CC_MUSIC:
        if (music_current() >= 0 && music_state() != MUSIC_STOPPED)
            music_toggle_pause();
        else if (music_count())
            music_play(ui->music_sel);
        else
            sound_play(SND_ERROR);
        return UI_NONE;
    case CC_SCREENSHOT:
        ui->cc_open = 0;
        ui->cc_anim = 0;
        osd_request_screenshot();
        return UI_NONE;
    case CC_SETTINGS:
        ui->cc_open = 0;
        return UI_SETTINGS;
    case CC_RESTART:
    case CC_OFF:
        if (!ui->cc_confirm) { /* som i inställningarna: ✕ två gånger */
            ui->cc_confirm = 1;
            ui->cc_label = 0;
            sound_play(SND_SELECT);
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
        ui_open_cc(ui);
        in = &none;
    }
    if (ui->menu_open) {
        u32 ok = ui->confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
        u32 back = (ui->confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE) | PSP_CTRL_SQUARE | PSP_CTRL_TRIANGLE;
        int move = step_input(ui, in, dt, 1);
        int items = menu_count(ui);
        if ((move == 2 || move == -2) && items > 1) {
            ui->menu_sel = (ui->menu_sel + move / 2 + items) % items;
            sound_play(SND_MOVE);
        }
        if (in->pressed & ok)
            menu_choose(ui);
        else if (in->pressed & back) {
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
    if ((in->pressed & PSP_CTRL_SELECT) && ui->intro >= 1) {
        if (ui->tab == TAB_GAMES || ui->tab == TAB_LIBRARY)
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

    /* L och R bläddrar direkt genom toppradens 4 val: Games (0) → Media (1) → Search (2) → Settings (3) */
    if ((in->pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) && in != &none && ui->intro >= 1) {
        int sec = 0;
        if (ui->tab == TAB_MEDIA || ui->tab == TAB_MUSIC || ui->tab == TAB_PICTURES || ui->tab == TAB_VIDEO)
            sec = 1;
        else if (ui->tab == TAB_SEARCH)
            sec = 2;
        int step = (in->pressed & PSP_CTRL_RTRIGGER) ? 1 : -1;
        int next_sec = (sec + step + 4) % 4;
        if (next_sec == 3)
            return UI_SETTINGS;
        static const int sec_tabs[3] = { TAB_GAMES, TAB_MEDIA, TAB_SEARCH };
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
        ensure_pics();
        ensure_videos(ui);
        int move_m = step_input(ui, in, dt, 0);
        if (move_m) {
            int next = (ui->sel[TAB_MEDIA] + move_m + 3) % 3;
            if (next != ui->sel[TAB_MEDIA]) {
                int wrapped = abs(next - ui->sel[TAB_MEDIA]) > 1;
                ui->sel[TAB_MEDIA] = next;
                if (wrapped)
                    ui->pos = next + (move_m > 0 ? -0.7f : 0.7f);
                ui->still = 0;
                sound_play(SND_MOVE);
            }
        }
        if ((in->pressed & confirm) && ui->intro >= 1) {
            static const int media_tabs[3] = { TAB_MUSIC, TAB_PICTURES, TAB_VIDEO };
            switch_tab(ui, media_tabs[(int)clampf(ui->sel[TAB_MEDIA], 0, 2)]);
        }
    }

    int count = ui->tab < TAB_MUSIC ? view(ui)->count : 0;
    int row_count = ui->tab == TAB_GAMES && count ? count + 1 : count; /* +1 för Spelbibliotekskortet längst till höger */
    int *sel = &ui->sel[ui->tab];
    int grid = ui->tab == TAB_LIBRARY;
    if (grid && !overlay && (in->pressed & back_btn) && ui->intro >= 1) {
        switch_tab(ui, TAB_GAMES);
        sound_play(SND_BACK);
    }
    if (grid && (in->pressed & PSP_CTRL_TRIANGLE) && ui->intro >= 1 && ui->launch < 0)
        switch_tab(ui, TAB_SEARCH); /* △: snabbt till sökningen */
    if ((ui->tab == TAB_GAMES || grid) && (in->pressed & PSP_CTRL_SQUARE) && ui_selected(ui) >= 0 &&
        ui->intro >= 1 && ui->launch < 0 && ui->list->items[ui_selected(ui)].type != ENTRY_UMD) {
        ui->menu_open = 1;
        ui->menu_sel = 0;
        sound_play(SND_OPEN);
    }
    int move = ui->tab >= TAB_MUSIC || overlay ? 0 : step_input(ui, in, dt, grid);
    if (move && row_count) {
        int next = *sel;
        if (move == 2 || move == -2) {
            int rows = (row_count + GRID_COLS - 1) / GRID_COLS;
            int r = *sel / GRID_COLS, c = *sel % GRID_COLS;
            r = (r + (move > 0 ? 1 : -1) + rows) % rows;
            next = r * GRID_COLS + c;
            if (next >= row_count)
                next = row_count - 1;
        } else {
            next = (*sel + move + row_count) % row_count;
        }
        if (next != *sel) {
            int wrapped = !grid && abs(next - *sel) > 1;
            *sel = next;
            if (wrapped)
                ui->pos = next + (move > 0 ? -0.7f : 0.7f);
            ui->still = 0;
            sound_play(SND_MOVE);
        }
    }
    ui->still += dt;

    /* START eller ✕ startar valt spel – eller öppnar Spelbiblioteket om sista kortet är valt */
    if (ui->tab < TAB_MUSIC && (in->pressed & confirm) && row_count && ui->intro >= 1) {
        if (ui->tab == TAB_GAMES && *sel == count) {
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

    ui->pos = approach(ui->pos, *sel, 13.0f, dt);
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
    const Entry *pv_entry = NULL;
    if (ui->tab == TAB_VIDEO && s_nvids > 0 && ui->vid_still >= 0.14f && !ui->cc_open) {
        pv = VID_PREVIEW + ui->vid_sel;
        pv_entry = &s_vids[ui->vid_sel].entry;
    } else if (g_previews && ui->tab == TAB_GAMES && !overlay && ui->cat_anim <= 0 &&
               ui->cc_anim <= 0 && ui->intro >= 1 && ui->still >= PREVIEW_DELAY) {
        pv = ui_selected(ui);
        if (pv >= 0)
            pv_entry = &ui->list->items[pv];
    }
    preview_want(pv_entry, pv);
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

/* blandar två färger (RGB), t = 0 → a, 1 → b, helt ogenomskinlig */
static u32 mix(u32 a, u32 b, float t)
{
    u32 out = 0xFF000000;
    for (int s = 0; s < 24; s += 8)
        out |= (u32)lerpf((a >> s) & 0xFF, (b >> s) & 0xFF, clampf(t, 0, 1)) << s;
    return out;
}

/* Sök- och inställningsikoner uppe till höger i toppraden (som på PS5) */
static void draw_search_icon(float cx, float cy, u32 c)
{
    gfx_ring(cx - 5.2f, cy - 5.2f, 8.8f, 8.8f, 4.4f, c);
    gfx_ring(cx - 4.4f, cy - 4.4f, 7.2f, 7.2f, 3.6f, c);
    float h[4] = { cx + 1.8f, cy + 1.8f, cx + 5.2f, cy + 5.2f };
    u32 hc[2] = { c, c };
    gfx_stroke(h, hc, 2.0f, 2);
}

static void draw_gear_icon(float cx, float cy, u32 c)
{
    for (int k = 0; k < 8; k++)
        gfx_circle(cx + cosf(k * 0.785398f) * 4.8f, cy + sinf(k * 0.785398f) * 4.8f, 1.5f, c);
    for (float r = 2.2f; r <= 4.3f; r += 0.7f)
        gfx_ring(cx - r, cy - r, r * 2, r * 2, r, c);
}

static void draw_top_bar(Ui *ui)
{
    float a = ui->intro;

    ScePspDateTime now;
    char clock[8] = "";
    if (read_clock(&now))
        snprintf(clock, sizeof(clock), "%02d:%02d", now.hour, now.minute);
    text_draw(SCREEN_W - MARGIN, 24.5f, 0.74f, 0, with_alpha(TEXT_MAIN, a), ALIGN_RIGHT, clock);
    float cw = text_width(0.74f, 0, clock);
    float bx = SCREEN_W - MARGIN - cw - 30;
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
        text_draw(bx - 5, 24.0f, 0.58f, 0, with_alpha(TEXT_DIM, a), ALIGN_RIGHT, info);
    draw_battery(bx, 15.0f, level, charging, ui->time, a, iw);
    float status_left = bx - (charging ? 10 : 0) - iw;

    /* PS5-topprad till höger: Sök (🔍) och Inställningar (⚙) till vänster om batteriet & klockan */
    float gear_cx = status_left - 18.0f, search_cx = status_left - 44.0f, icon_cy = 19.5f;
    int on_search = ui->tab == TAB_SEARCH;
    if (on_search) {
        gfx_circle(search_cx, icon_cy, 11.0f, RGBA(255, 255, 255, (int)(36 * a)));
        gfx_ring(search_cx - 11.0f, icon_cy - 11.0f, 22.0f, 22.0f, 11.0f, RGBA(255, 255, 255, (int)(190 * a)));
    }
    draw_search_icon(search_cx, icon_cy, on_search ? with_alpha(TEXT_MAIN, a) : RGBA(255, 255, 255, (int)(135 * a)));
    draw_gear_icon(gear_cx, icon_cy, RGBA(255, 255, 255, (int)(135 * a)));
    status_left = search_cx - 14.0f;

    /* PS5-topprad till vänster: bara Spel och Media i originalets rena typografi */
    int on_games = ui->tab == TAB_GAMES || ui->tab == TAB_LIBRARY;
    int on_media = ui->tab == TAB_MEDIA || ui->tab == TAB_MUSIC || ui->tab == TAB_PICTURES || ui->tab == TAB_VIDEO;
    char gname[64];
    int gcat = ui->tab == TAB_LIBRARY ? ui->cat[TAB_LIBRARY] : ui->cat[TAB_GAMES];
    if (on_games && gcat > 0)
        snprintf(gname, sizeof(gname), "%s · %s", L(S_TAB_GAMES), cat_title(gcat));
    else
        snprintf(gname, sizeof(gname), "%s", L(S_TAB_GAMES));

    float x = MARGIN;
    u32 gc = on_games ? with_alpha(TEXT_MAIN, a) : RGBA(255, 255, 255, (int)(120 * a));
    text_draw(x, 25, 0.8f, on_games, gc, ALIGN_LEFT, gname);
    x += text_width(0.8f, on_games, gname) + 22;

    const char *mname = L(S_TAB_MEDIA);
    u32 mc = on_media ? with_alpha(TEXT_MAIN, a) : RGBA(255, 255, 255, (int)(120 * a));
    text_draw(x, 25, 0.8f, on_media, mc, ALIGN_LEFT, mname);
    x += text_width(0.8f, on_media, mname) + 22;

    /* musik som spelas syns som en liten not med titeln i de andra flikarna */
    int cur = music_current();
    if (ui->tab != TAB_MUSIC && cur >= 0 && music_state() != MUSIC_STOPPED) {
        float right = status_left - 10, left = x + 4;
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
            draw_note(right - tw - 8, 20.0f, 10, with_alpha(LIGHT, pulse * a));
            text_draw(right, 24.0f, 0.5f, 0, with_alpha(TEXT_DIM, a), ALIGN_RIGHT, line);
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

    /* fäst: en liten nål i övre högra hörnet */
    if (meta(list_index)->pinned) {
        float s = w / BIG_W, bx = x + w - 8 * s - 2, by = y + 8 * s + 2;
        gfx_circle(bx, by, 6.5f * s, RGBA(18, 18, 22, (int)(200 * alpha)));
        gfx_circle(bx, by - 1.4f * s, 2.4f * s, RGBA(240, 240, 244, (int)(255 * alpha)));
        gfx_rect(bx - 0.6f * s, by - 0.5f * s, 1.2f * s, 4.2f * s, RGBA(240, 240, 244, (int)(255 * alpha)));
    }

    /* valt kort: tunn ljus ram med ett litet glapp */
    if (focus > 0.01f)
        gfx_ring(x - 4, y - 4, w + 8, h + 8, r + 4, RGBA(240, 240, 244, (int)(235 * focus * alpha)));
}

/* PS5 Game Library-kortet längst till höger i spelraden: 3×2-rutnät + liten handkontroll */
static void draw_library_card(Ui *ui, float x, float y, float w, float h, float focus, float alpha)
{
    (void)ui;
    float r = lerpf(5.0f, BIG_R, (w - SMALL_W) / (BIG_W - SMALL_W));
    float s = w / BIG_W;
    gfx_glow(x + 1, y + 3, w - 2, h - 2, 5 + 4 * focus, RGBA(0, 0, 0, (int)(130 * alpha)));
    u32 cc = theme_cur()->card;
    gfx_round_rect(x, y, w, h, r, RGBA((cc & 0xFF) + 4, ((cc >> 8) & 0xFF) + 5,
                                       ((cc >> 16) & 0xFF) + 8, (int)(238 * alpha)));

    float cx = x + w / 2, cy = y + h / 2;
    u32 ic = RGBA(240, 242, 248, (int)((185 + 65 * focus) * alpha));
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
        gfx_ring(x - 4, y - 4, w + 8, h + 8, r + 4, RGBA(240, 240, 244, (int)(235 * focus * alpha)));
}

static void draw_row(Ui *ui)
{
    const LoaderView *v = view(ui);
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
    float fade = ui->intro * smooth(ui->tab_anim) * (ui->launch < 0 ? 1 : 1 - smooth(ui->launch * 2));
    if (fade <= 0)
        return;

    /* Spelbibliotek-kortet längst till höger i Spel-raden */
    if (ui->tab == TAB_GAMES && view(ui)->count > 0 && ui->sel[TAB_GAMES] == view(ui)->count) {
        float la = smooth(ui->label_anim);
        text_draw(SMALL_X + (1 - la) * 8, LABEL_Y, ui->label_size, 0, with_alpha(TEXT_MAIN, la * fade),
                  ALIGN_LEFT, ui->label);
        const char *badge = "LIB";
        float bw = text_width(0.5f, 1, badge) + 12, y = 206;
        gfx_round_rect(MARGIN, y, bw, 15, 3, with_alpha(LIGHT, 0.92f * fade));
        text_draw(MARGIN + bw / 2, y + 11, 0.5f, 1, with_alpha(DARK, fade), ALIGN_CENTER, badge);
        char cnt[32];
        snprintf(cnt, sizeof(cnt), "%d", ui->views[TAB_LIBRARY].count);
        text_draw(MARGIN + bw + 8, y + 11.5f, 0.62f, 0, with_alpha(TEXT_DIM, fade), ALIGN_LEFT, cnt);

        const char *open = L(S_F_OPEN);
        float bx = MARGIN, by = 233, bh = 26;
        float tw = text_width(0.66f, 0, open);
        float w = 8 + 16 + 8 + tw + 14;
        gfx_glow(bx, by + 2, w, bh, 6, RGBA(0, 0, 0, (int)(110 * fade)));
        gfx_round_rect(bx, by, w, bh, bh / 2, with_alpha(LIGHT, 0.95f * fade));
        gfx_button(bx + 16, by + bh / 2, 8, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, with_alpha(DARK, fade),
                   with_alpha(LIGHT, fade));
        text_draw(bx + 32, by + 17.5f, 0.66f, 0, with_alpha(DARK, fade), ALIGN_LEFT, open);
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
    text_draw(SMALL_X + (1 - la) * 8, LABEL_Y, ui->label_size, 0, with_alpha(TEXT_MAIN, la * fade),
              ALIGN_LEFT, ui->label);

    /* nere till vänster: typetikett, spel-ID och startknapp (i linje med knapptipsen på y = 246) */
    const char *type = type_name(e->type);
    float bw = text_width(0.5f, 1, type) + 12;
    float y = 206;
    gfx_round_rect(MARGIN, y, bw, 15, 3, with_alpha(LIGHT, 0.92f * fade));
    text_draw(MARGIN + bw / 2, y + 11, 0.5f, 1, with_alpha(DARK, fade), ALIGN_CENTER, type);
    if (e->type != ENTRY_HOMEBREW && e->game_id[0])
        text_draw(MARGIN + bw + 8, y + 11.5f, 0.62f, 0, with_alpha(TEXT_DIM, fade), ALIGN_LEFT, e->game_id);

    const char *go = L(S_LAUNCH);
    float bx = MARGIN, by = 233, bh = 26;
    float tw = text_width(0.66f, 0, go);
    float w = 8 + 16 + 8 + tw + 14;
    gfx_glow(bx, by + 2, w, bh, 6, RGBA(0, 0, 0, (int)(110 * fade)));
    gfx_round_rect(bx, by, w, bh, bh / 2, with_alpha(LIGHT, 0.95f * fade));
    gfx_button(bx + 8 + 8, by + bh / 2, 8, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, with_alpha(DARK, fade), with_alpha(LIGHT, fade));
    text_draw(bx + 32, by + 17.5f, 0.66f, 0, with_alpha(DARK, fade), ALIGN_LEFT, go);

    /* □ Alternativ (inte för UMD-skivan) – på samma y = 246 som L R-tipsen till höger */
    if (e->type == ENTRY_UMD)
        return;
    float oa = fade * (1 - smooth(ui->menu_anim));
    float mx = bx + w + 16, my = by + bh / 2;
    float ow = 13 + 6 + text_width(0.6f, 0, L(S_OPTIONS));
    gfx_round_rect(mx - 9, my - 11, ow + 18, 22, 11, RGBA(0, 0, 0, (int)(120 * oa)));
    gfx_button(mx + 6.5f, my, 6.5f, BTN_SQUARE, with_alpha(LIGHT, 0.9f * oa), with_alpha(DARK, oa));
    text_draw(mx + 19, my + 4.5f, 0.6f, 0, with_alpha(TEXT_MAIN, oa), ALIGN_LEFT, L(S_OPTIONS));
}

/* Media-raden: tre PS5-kort för Musik, Bilder och Videor */
static void draw_media_card(int kind, float x, float y, float w, float h, float focus, float alpha)
{
    float r = lerpf(5.0f, BIG_R, (w - SMALL_W) / (BIG_W - SMALL_W));
    float s = w / BIG_W;
    gfx_glow(x + 1, y + 3, w - 2, h - 2, 5 + 4 * focus, RGBA(0, 0, 0, (int)(130 * alpha)));
    u32 cc = theme_cur()->card;
    gfx_round_rect(x, y, w, h, r, RGBA((cc & 0xFF) + 6, ((cc >> 8) & 0xFF) + 7,
                                       ((cc >> 16) & 0xFF) + 12, (int)(238 * alpha)));
    float cx = x + w / 2, cy = y + h / 2;
    u32 ic = RGBA(242, 244, 250, (int)((185 + 65 * focus) * alpha));
    u32 hole = RGBA((cc & 0xFF) + 6, ((cc >> 8) & 0xFF) + 7, ((cc >> 16) & 0xFF) + 12, (int)(238 * alpha));
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
        gfx_ring(x - 4, y - 4, w + 8, h + 8, r + 4, RGBA(240, 240, 244, (int)(235 * focus * alpha)));
}

static void draw_media_row(Ui *ui)
{
    ensure_pics();
    ensure_videos(ui);
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

    float la = smooth(ui->label_anim);
    text_draw(SMALL_X + (1 - la) * 8, LABEL_Y, ui->label_size, 0, with_alpha(TEXT_MAIN, la * row_alpha),
              ALIGN_LEFT, ui->label);

    static const char *const badges[3] = { "MP3", "IMG", "PMF" };
    int counts[3] = { music_count(), s_npics, s_nvids };
    int sel = clampf(ui->sel[TAB_MEDIA], 0, 2);
    const char *badge = badges[sel];
    float bw = text_width(0.5f, 1, badge) + 12, y = 206;
    gfx_round_rect(MARGIN, y, bw, 15, 3, with_alpha(LIGHT, 0.92f * row_alpha));
    text_draw(MARGIN + bw / 2, y + 11, 0.5f, 1, with_alpha(DARK, row_alpha), ALIGN_CENTER, badge);
    char cnt[32];
    snprintf(cnt, sizeof(cnt), "%d", counts[sel]);
    text_draw(MARGIN + bw + 8, y + 11.5f, 0.62f, 0, with_alpha(TEXT_DIM, row_alpha), ALIGN_LEFT, cnt);

    const char *open = L(S_F_OPEN);
    float bx = MARGIN, by = 233, bh = 26;
    float tw = text_width(0.66f, 0, open);
    float w = 8 + 16 + 8 + tw + 14;
    gfx_glow(bx, by + 2, w, bh, 6, RGBA(0, 0, 0, (int)(110 * row_alpha)));
    gfx_round_rect(bx, by, w, bh, bh / 2, with_alpha(LIGHT, 0.95f * row_alpha));
    gfx_button(bx + 16, by + bh / 2, 8, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, with_alpha(DARK, row_alpha),
               with_alpha(LIGHT, row_alpha));
    text_draw(bx + 32, by + 17.5f, 0.66f, 0, with_alpha(DARK, row_alpha), ALIGN_LEFT, open);
}

/* Alternativmenyn (□): en panel nere till vänster, över Starta-knappen, med kortets namn,
 * fäst, flytta, sortering och ✕ / ○ längst ner. */
static void draw_menu(Ui *ui)
{
    float a = smooth(ui->menu_anim);
    int i = ui_selected(ui);
    if (a <= 0 || i < 0)
        return;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(110 * a)));
    const float row = 26;
    float w = 214, h = 26 + menu_count(ui) * row + 34, x = MARGIN - 6, y = 258 - h + (1 - a) * 10;
    gfx_glow(x, y, w, h, 10, RGBA(0, 0, 0, (int)(150 * a)));
    gfx_round_rect(x, y, w, h, 9, RGBA(40, 41, 47, (int)(255 * a)));

    char title[160];
    text_ellipsis(title, sizeof(title), ui->list->items[i].title, 0.5f, w - 24);
    text_draw(x + 12, y + 18, 0.5f, 0, with_alpha(TEXT_DIM, a), ALIGN_LEFT, title);

    for (int k = 0; k < menu_count(ui); k++) {
        char buf[64];
        const char *item = menu_label(ui, k, buf, sizeof(buf));
        float ry = y + 26 + k * row;
        int sel = k == ui->menu_sel;
        if (sel)
            gfx_round_rect(x + 6, ry, w - 12, row - 2, 7, with_alpha(LIGHT, 0.95f * a));
        text_draw(x + 16, ry + 17, text_fit(0.6f, 0.45f, 0, w - 32, item), 0, with_alpha(sel ? DARK : TEXT_MAIN, a),
                  ALIGN_LEFT, item);
    }

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
    const char *tab = L(S_HINT_TAB);
    float w = hints_width();
    float x = SCREEN_W - MARGIN - w, y = 246;

    gfx_round_rect(x - 9, y - 11, w + 18, 22, 11, RGBA(0, 0, 0, (int)(120 * a)));
    x += key_cap(x, y, "L", a) + 3;
    x += key_cap(x, y, "R", a) + 6;
    text_draw(x, y + 4.5f, 0.6f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, tab);
}

static float hints_width(void)
{
    static float w;
    static const char *for_lang; /* bredden ändras bara med språket */
    if (for_lang != L(S_HINT_TAB)) {
        float lw = text_width(0.42f, 1, "L") + 9, rw = text_width(0.42f, 1, "R") + 9;
        w = lw + 3 + rw + 6 + text_width(0.6f, 0, L(S_HINT_TAB));
        for_lang = L(S_HINT_TAB);
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

    /* till höger: △ Sök   □ Alternativ (fäst, sortering) */
    float rx = SCREEN_W - MARGIN;
    text_draw(rx, 53, 0.56f, 0, with_alpha(TEXT_DIM, ui_a), ALIGN_RIGHT, L(S_OPTIONS));
    rx -= text_width(0.56f, 0, L(S_OPTIONS)) + 11;
    gfx_button(rx, 49, 6, BTN_SQUARE, with_alpha(LIGHT, 0.85f * ui_a), with_alpha(DARK, ui_a));
    rx -= 16;
    text_draw(rx, 53, 0.56f, 0, with_alpha(TEXT_DIM, ui_a), ALIGN_RIGHT, L(S_SEARCH));
    rx -= text_width(0.56f, 0, L(S_SEARCH)) + 11;
    gfx_button(rx, 49, 6, BTN_TRIANGLE, with_alpha(LIGHT, 0.85f * ui_a), with_alpha(DARK, ui_a));

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

/* Albumlistan i musikfliken: först "Alla låtar", sedan ett album per rad (namn, artist · antal) */
static void draw_album_rows(Ui *ui, float a, float slide, float lw)
{
    static RowCache cache[MROWS + 3];
    static int cache_gen = -1, cache_n = -1;
    static const char *cache_lang;
    if (cache_gen != s_alb_gen || cache_n != s_alb_count || cache_lang != L(S_ALL_TRACKS)) {
        for (int i = 0; i < MROWS + 3; i++)
            cache[i].idx = -1;
        cache_gen = s_alb_gen;
        cache_n = s_alb_count;
        cache_lang = L(S_ALL_TRACKS);
    }
    int first = (int)floorf(ui->album_scroll), last = first + MROWS;
    for (int i = first < 0 ? 0 : first; i <= last && i <= s_nalb; i++) {
        float y = MLIST_Y + (i - ui->album_scroll) * MROW_H + slide;
        RowCache *c = &cache[i % (MROWS + 3)];
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
                          0.6f, lw - 30);
            text_ellipsis(c->sub, sizeof(c->sub), sub, 0.46f, lw - 30);
            c->idx = i;
        }
        int selected = i == ui->album_sel;
        if (selected)
            gfx_round_rect(MARGIN - 6, y, lw + 12, MROW_H - 2, 7, with_alpha(LIGHT, 0.93f * a));
        u32 main = selected ? with_alpha(DARK, a) : with_alpha(TEXT_MAIN, a);
        u32 dim = selected ? RGBA(70, 72, 80, (int)(255 * a)) : with_alpha(TEXT_DIM, a);
        draw_note(MARGIN + 7, y + 12, 13, i == ui->music_album ? with_alpha(theme_cur()->accent, a) : dim);
        text_draw(MARGIN + 20, y + 11.5f, 0.6f, 0, main, ALIGN_LEFT, c->title);
        text_draw(MARGIN + 20, y + 22, 0.46f, 0, dim, ALIGN_LEFT, c->sub);
    }
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
    /* blanda (□): uppe till höger, i temats färg när det är på, svag när det är av */
    draw_shuffle(px + pw - 20, py + 13, g_shuffle ? with_alpha(theme_cur()->accent, a) : with_alpha(LIGHT, 0.3f * a));
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
    int lo, hi;
    music_range(ui, &lo, &hi);
    if (ui->music_view)
        draw_album_rows(ui, a, slide, lw);
    int first = (int)floorf(ui->music_scroll), last = first + MROWS;
    for (int i = first; !ui->music_view && i <= last && i < hi; i++) {
        if (i < lo)
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

    /* knapptips nere till vänster: ✕ Spela/Paus  □ Blanda  ◀ ▶ Spår */
    float x = MARGIN, y = 246;
    const char *play = L(ui->music_view ? S_SELECT : ui->music_sel == cur && state == MUSIC_PLAYING ? S_PAUSE : S_PLAY);
    gfx_button(x + 6.5f, y, 6.5f, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    x += 17;
    text_draw(x, y + 4.5f, 0.56f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, play);
    x += text_width(0.56f, 0, play) + 12;
    gfx_button(x + 6.5f, y, 6.5f, BTN_SQUARE, with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    x += 17;
    text_draw(x, y + 4.5f, 0.56f, 0, g_shuffle ? with_alpha(theme_cur()->accent, a) : with_alpha(TEXT_MAIN, a),
              ALIGN_LEFT, L(S_SHUFFLE));
    x += text_width(0.56f, 0, L(S_SHUFFLE)) + 12;
    gfx_button(x + 6.5f, y, 6.5f, BTN_TRIANGLE, with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    x += 17;
    text_draw(x, y + 4.5f, 0.56f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, L(S_ALBUMS));
    x += text_width(0.56f, 0, L(S_ALBUMS)) + 12;
    float l[6] = { x, y, x + 6, y - 4.5f, x + 6, y + 4.5f };
    gfx_fan(x + 3, y, l, 3, with_alpha(LIGHT, 0.9f * a));
    float r[6] = { x + 15, y, x + 9, y - 4.5f, x + 9, y + 4.5f };
    gfx_fan(x + 12, y, r, 3, with_alpha(LIGHT, 0.9f * a));
    /* krymper hellre än krockar med L R-tipsen till höger */
    float tsize = text_fit(0.56f, 0.4f, 0, SCREEN_W - MARGIN - hints_width() - 26 - (x + 20), L(S_TRACK));
    text_draw(x + 20, y + 4.5f, tsize, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, L(S_TRACK));
}

/* Bildfliken: bildlista till vänster, förhandsvisning med upplösning till höger, helskärm med ✕ */
static void draw_pictures(Ui *ui)
{
    ensure_pics();
    float a = ui->intro * smooth(ui->tab_anim);
    float slide = (1 - smooth(ui->tab_anim)) * 10;
    int n = s_npics;

    /* --- förhandsvisning till höger --- */
    float px = PANEL_X, pw = SCREEN_W - MARGIN - PANEL_X, py = MLIST_Y + slide, ph = MROWS * MROW_H - 2;
    gfx_round_rect(px, py, pw, ph, 10, RGBA(255, 255, 255, (int)(16 * a)));
    float bx = px + 10, by = py + 10, bw = pw - 20, bh = ph - 44;
    gfx_round_rect(bx, by, bw, bh, 7, RGBA(18, 19, 24, (int)(200 * a)));

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
        text_draw(px + pw / 2, py + ph - 14, 0.48f, 0, with_alpha(TEXT_DIM, a), ALIGN_CENTER, info);
    } else if (n > 0) {
        char info[48];
        snprintf(info, sizeof(info), "%d / %d", ui->pic_sel + 1, n);
        text_draw(px + pw / 2, py + ph - 14, 0.48f, 0, with_alpha(TEXT_DIM, a), ALIGN_CENTER, info);
    }

    if (!n) {
        text_draw(MARGIN, 120, 0.9f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, L(S_NO_PICTURES));
        text_draw(MARGIN, 142, 0.62f, 0, with_alpha(TEXT_DIM, a), ALIGN_LEFT, on_root(L(S_HINT_PICTURES)));
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
            gfx_round_rect(MARGIN - 6, y, lw + 12, MROW_H - 2, 7, with_alpha(LIGHT, 0.93f * a));
        u32 main = selected ? with_alpha(DARK, a) : with_alpha(TEXT_MAIN, a);
        u32 dim = selected ? RGBA(70, 72, 80, (int)(255 * a)) : with_alpha(TEXT_DIM, a);

        /* liten bildikon till vänster */
        int is_wall = g_wallpaper[0] && !strcmp(g_wallpaper, s_pics[i].path);
        u32 ic = is_wall ? with_alpha(theme_cur()->accent, a) : dim;
        gfx_round_rect(MARGIN + 1, y + 7, 13, 10, 2, ic);
        gfx_circle(MARGIN + 5, y + 10.5f, 1.3f, selected ? with_alpha(LIGHT, a) : RGBA(24, 25, 30, (int)(255 * a)));

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
    gfx_button(x + 6.5f, y, 6.5f, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    x += 17;
    text_draw(x, y + 4.5f, 0.56f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, open);
    x += text_width(0.56f, 0, open) + 12;
    gfx_button(x + 6.5f, y, 6.5f, BTN_SQUARE, with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    x += 17;
    float wsize = text_fit(0.56f, 0.4f, 0, SCREEN_W - MARGIN - hints_width() - 22 - x, wall);
    text_draw(x, y + 4.5f, wsize, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, wall);
}

static void draw_pic_fullscreen(Ui *ui)
{
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
    gfx_button(x + 6, y, 6, ui->confirm_circle ? BTN_CROSS : BTN_CIRCLE, LIGHT, DARK);
    x += 16;
    text_draw(x, y + 4, 0.52f, 0, TEXT_MAIN, ALIGN_LEFT, back);
    x += text_width(0.52f, 0, back) + 14;
    gfx_button(x + 6, y, 6, BTN_SQUARE, LIGHT, DARK);
    x += 16;
    text_draw(x, y + 4, 0.52f, 0, TEXT_MAIN, ALIGN_LEFT, wall);
    x += text_width(0.52f, 0, wall) + 14;
    text_draw(x, y + 4, 0.52f, 0, TEXT_DIM, ALIGN_LEFT, cnt);
}

/* Videofliken: videolista till vänster, PMF/MPS-uppspelning till höger, helskärm med ✕ */
static void draw_videos(Ui *ui)
{
    ensure_videos(ui);
    float a = ui->intro * smooth(ui->tab_anim);
    float slide = (1 - smooth(ui->tab_anim)) * 10;
    int n = s_nvids;

    /* --- videoförhandsvisning till höger --- */
    float px = PANEL_X, pw = SCREEN_W - MARGIN - PANEL_X, py = MLIST_Y + slide, ph = MROWS * MROW_H - 2;
    gfx_round_rect(px, py, pw, ph, 10, RGBA(255, 255, 255, (int)(16 * a)));
    float bx = px + 10, by = py + 10, bw = pw - 20, bh = ph - 44;
    gfx_round_rect(bx, by, bw, bh, 7, RGBA(18, 19, 24, (int)(210 * a)));

    if (n > 0 && ui->vid_sel < n) {
        Texture *vt = ui->preview_anim > 0 ? preview_frame(VID_PREVIEW + ui->vid_sel) : NULL;
        if (vt) {
            float va = smooth(ui->preview_anim);
            float s = fminf(bw / vt->w, bh / vt->h);
            float iw = vt->w * s, ih = vt->h * s;
            float ix = bx + (bw - iw) / 2, iy = by + (bh - ih) / 2;
            gfx_glow(ix, iy + 2, iw, ih, 6, RGBA(0, 0, 0, (int)(130 * a * va)));
            gfx_tex(vt, ix, iy, iw, ih, RGBA(255, 255, 255, (int)(255 * a * va)));
        } else {
            gfx_circle(bx + bw / 2, by + bh / 2, 13, RGBA(0, 0, 0, (int)(140 * a)));
            draw_play_glyph(bx + bw / 2 + 1, by + bh / 2, 7.5f, 0, with_alpha(LIGHT, 0.9f * a));
        }
        char info[64];
        if (vt)
            snprintf(info, sizeof(info), "%d × %d  ·  %d / %d", vt->w, vt->h, ui->vid_sel + 1, n);
        else
            snprintf(info, sizeof(info), "%d / %d", ui->vid_sel + 1, n);
        text_draw(px + pw / 2, py + ph - 14, 0.48f, 0, with_alpha(TEXT_DIM, a), ALIGN_CENTER, info);
    }

    if (!n) {
        text_draw(MARGIN, 120, 0.9f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, L(S_NO_VIDEOS));
        text_draw(MARGIN, 142, 0.62f, 0, with_alpha(TEXT_DIM, a), ALIGN_LEFT, on_root(L(S_HINT_VIDEOS)));
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
            gfx_round_rect(MARGIN - 6, y, lw + 12, MROW_H - 2, 7, with_alpha(LIGHT, 0.93f * a));
        u32 main = selected ? with_alpha(DARK, a) : with_alpha(TEXT_MAIN, a);
        u32 dim = selected ? RGBA(70, 72, 80, (int)(255 * a)) : with_alpha(TEXT_DIM, a);

        gfx_round_rect(MARGIN + 1, y + 7, 13, 10, 2, dim);
        draw_play_glyph(MARGIN + 7.8f, y + 12.0f, 3.6f, 0,
                        selected ? with_alpha(LIGHT, a) : RGBA(24, 25, 30, (int)(255 * a)));

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
    gfx_button(x + 6.5f, y, 6.5f, ui->confirm_circle ? BTN_CIRCLE : BTN_CROSS, with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    x += 17;
    text_draw(x, y + 4.5f, 0.56f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, open);
    x += text_width(0.56f, 0, open) + 12;
    gfx_button(x + 6.5f, y, 6.5f, ui->confirm_circle ? BTN_CROSS : BTN_CIRCLE, with_alpha(LIGHT, 0.9f * a), with_alpha(DARK, a));
    x += 17;
    text_draw(x, y + 4.5f, 0.56f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, back);
}

static void draw_vid_fullscreen(Ui *ui)
{
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(6, 6, 10, 252));
    Texture *vt = preview_frame(VID_PREVIEW + ui->vid_sel);
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
    gfx_button(x + 6, y, 6, ui->confirm_circle ? BTN_CROSS : BTN_CIRCLE, LIGHT, DARK);
    x += 16;
    text_draw(x, y + 4, 0.52f, 0, TEXT_MAIN, ALIGN_LEFT, back);
    x += text_width(0.52f, 0, back) + 14;
    text_draw(x, y + 4, 0.52f, 0, TEXT_DIM, ALIGN_LEFT, cnt);
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

/* Sökfliken (🔍): PS5-sökfält med tangentrad och direktfiltrerade spel/appar */
static void draw_search(Ui *ui)
{
    float a = ui->intro * smooth(ui->tab_anim);
    float slide = (1 - smooth(ui->tab_anim)) * 8;
    float launch = ui->launch < 0 ? 0 : smooth(ui->launch);
    a *= 1 - smooth(launch * 2);

    int hits[512];
    int n = collect_search(ui, hits, 512);

    /* sökfältet högst upp */
    float sx = MARGIN, sy = 42 + slide, sw = SCREEN_W - 2 * MARGIN, sh = 24;
    gfx_round_rect(sx, sy, sw, sh, 12, RGBA(255, 255, 255, (int)((ui->search_focus == 0 ? 28 : 18) * a)));
    if (ui->search_focus == 0)
        gfx_ring(sx, sy, sw, sh, 12, RGBA(255, 255, 255, (int)(130 * a)));
    draw_search_icon(sx + 14, sy + 12, with_alpha(TEXT_MAIN, a));

    if (ui->search_query[0]) {
        text_draw(sx + 28, sy + 16.5f, 0.62f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, ui->search_query);
        float qw = text_width(0.62f, 0, ui->search_query);
        if (ui->search_focus == 0 && ((int)(ui->time * 2.5f) & 1) == 0)
            gfx_rect(sx + 30 + qw, sy + 6, 1.5f, 12, with_alpha(LIGHT, a));
    } else {
        text_draw(sx + 28, sy + 16.5f, 0.6f, 0, with_alpha(TEXT_DIM, a), ALIGN_LEFT, L(S_SEARCH));
    }
    char cnt[24];
    snprintf(cnt, sizeof(cnt), "%d", n);
    text_draw(sx + sw - 12, sy + 16.5f, 0.54f, 0, with_alpha(TEXT_DIM, a), ALIGN_RIGHT, cnt);

    /* tangentbordet till vänster: 7 tangenter per rad */
    float kw = 22, kh = 20, kgap = 3, ky0 = sy + 34;
    for (int k = 0; k < SEARCH_KEY_COUNT; k++) {
        float kx = sx + (k % SEARCH_COLS) * (kw + kgap), ky = ky0 + (k / SEARCH_COLS) * (kh + kgap);
        int sel = k == ui->search_key;
        if (sel) {
            u32 kc = ui->search_focus == 0 ? with_alpha(LIGHT, 0.95f * a) : RGBA(255, 255, 255, (int)(55 * a));
            gfx_round_rect(kx, ky, kw, kh, 4.5f, kc);
        } else {
            gfx_round_rect(kx, ky, kw, kh, 4.5f, RGBA(255, 255, 255, (int)(16 * a)));
        }
        u32 tc = (sel && ui->search_focus == 0) ? with_alpha(DARK, a) : with_alpha(TEXT_MAIN, a);
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
    if (!n) {
        text_draw(rx + rw / 2, ry0 + 60, 0.72f, 0, with_alpha(TEXT_DIM, a), ALIGN_CENTER, L(S_NO_RESULTS));
    } else {
        gfx_clip((int)rx - 2, (int)ry0 - 2, (int)rw + 4, (int)(SEARCH_ROWS * rh + 2));
        int first = (int)floorf(ui->search_scroll), last = first + SEARCH_ROWS;
        for (int r = first; r <= last && r < n; r++) {
            if (r < 0)
                continue;
            float y = ry0 + (r - ui->search_scroll) * rh;
            int sel = ui->search_focus == 1 && r == ui->search_sel;
            if (sel)
                gfx_round_rect(rx, y, rw, rh - 3, 7, with_alpha(LIGHT, 0.94f * a));
            int idx = hits[r];
            const Entry *e = &ui->list->items[idx];
            IconSlot *slot = loader_icon(idx);
            float ix = rx + 6, iy = y + 3.5f, iw = 38, ih = 21;
            if (slot && slot->state == SLOT_READY)
                gfx_tex(slot->tex, ix, iy, iw, ih, RGBA(255, 255, 255, (int)(255 * a)));
            else
                gfx_round_rect(ix, iy, iw, ih, 3, RGBA(32, 34, 42, (int)(220 * a)));

            u32 main = sel ? with_alpha(DARK, a) : with_alpha(TEXT_MAIN, a);
            u32 dim = sel ? RGBA(70, 72, 80, (int)(255 * a)) : with_alpha(TEXT_DIM, a);
            char title[120];
            text_ellipsis(title, sizeof(title), e->title, 0.6f, rw - 110);
            text_draw(ix + iw + 10, y + 18.5f, 0.6f, 0, main, ALIGN_LEFT, title);
            text_draw(rx + rw - 10, y + 18.0f, 0.48f, 1, dim, ALIGN_RIGHT, type_name(e->type));
        }
        gfx_clip_reset();
    }

    draw_select_back(ui, MARGIN, 246, a);
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
    case CC_SCREENSHOT: /* kamera */
        gfx_round_rect(cx - 4, cy - 8, 8, 4, 1.5f, c);
        gfx_round_rect(cx - 9.5f, cy - 5.5f, 19, 13, 2.5f, c);
        gfx_circle(cx, cy + 1, 4.2f, bg);
        gfx_circle(cx, cy + 1, 2.4f, c);
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

/* Panelens färg: temats (som inställningarna, lite mörkare för texten) – grå med egen bakgrundsbild */
static void cc_colors(u32 *top, u32 *bottom)
{
    if (g_wallpaper[0]) {
        *top = RGBA(48, 49, 55, 255);
        *bottom = RGBA(26, 27, 31, 255);
        return;
    }
    const Theme *t = theme_cur();
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
    gfx_round_rect(ax, ay, as, as, 7, RGBA(70, 72, 84, (int)(255 * a)));
    draw_note(ax + as / 2, ay + as / 2, 16, with_alpha(LIGHT, 0.9f * a));
    if (ui->cover && ui->cover_for == cur)
        gfx_tex(ui->cover, ax, ay, as, as, RGBA(255, 255, 255, (int)(255 * a * smooth(ui->cover_fade))));
    text_draw(ax + as + 9, y + 20, 0.56f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, title);
    text_draw(ax + as + 9, y + 35, 0.46f, 0, with_alpha(TEXT_DIM, a), ALIGN_LEFT, artist);
    draw_play_glyph(x + w - 18, y + h / 2, 6, music_state() == MUSIC_PLAYING, with_alpha(LIGHT, a));
}

/* Control Center: en panel som glider upp nerifrån med en rad ikoner, som PS5:s. Den vita
 * markeringen glider mellan ikonerna och namnet på valt alternativ står ovanför. */
static void draw_cc(Ui *ui)
{
    float t = ui->cc_anim;
    if (t <= 0)
        return;
    float e = 1 - (1 - t) * (1 - t) * (1 - t); /* mjuk inbromsning */
    u32 top, bottom;
    cc_colors(&top, &bottom);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(110 * e)));

    const float ph = 84, step = 48;
    float py = SCREEN_H - ph * e;
    draw_cc_music(ui, MARGIN, py - 56, e, top);
    gfx_glow(0, py, SCREEN_W, ph + 20, 12, RGBA(0, 0, 0, (int)(150 * e)));
    gfx_round_rect(0, py, SCREEN_W, ph + 20, 16, with_alpha(top, 0.97f * e));
    gfx_gradient_v(0, py + 16, SCREEN_W, ph - 16, with_alpha(top, 0.97f * e), with_alpha(bottom, 0.97f * e));
    gfx_rect(16, py, SCREEN_W - 32, 1, RGBA(255, 255, 255, (int)(22 * e))); /* svag ljus kant upptill */

    float x0 = SCREEN_W / 2 - (CC_COUNT - 1) * step / 2, cy = py + 52;
    float hx = x0 + ui->cc_pos * step;
    gfx_glow(hx - 20, cy - 18, 40, 40, 6, RGBA(0, 0, 0, (int)(90 * e)));
    gfx_round_rect(hx - 20, cy - 20, 40, 40, 10, with_alpha(LIGHT, e));

    u32 hole = mix(top, bottom, 0.6f); /* panelens färg vid ikonerna – för "hålen" i ikonerna */
    for (int k = 0; k < CC_COUNT; k++) {
        float f = clampf(1 - fabsf(k - ui->cc_pos), 0, 1); /* hur mycket markeringen täcker ikonen */
        float ia = smooth(t * 1.7f - k * 0.08f);           /* ikonerna tonas in en i taget */
        float iy = cy + (1 - ia) * 10;
        draw_cc_glyph(k, x0 + k * step, iy, with_alpha(mix(LIGHT, DARK, f), ia * e),
                      with_alpha(mix(hole, LIGHT, f), ia * e));
    }

    /* namnet ovanför markeringen */
    const char *label = ui->cc_confirm ? L(S_CONFIRM) : cc_label(ui->cc_sel);
    float la = smooth(ui->cc_label) * e;
    float lw = text_width(0.6f, 0, label);
    float lx = clampf(x0 + ui->cc_sel * step, MARGIN + lw / 2, SCREEN_W - MARGIN - lw / 2);
    text_draw(lx, py + 22 + (1 - la) * 4, 0.6f, 0, with_alpha(TEXT_MAIN, la), ALIGN_CENTER, label);
}

static void draw_empty(Ui *ui)
{
    float a = ui->intro * smooth(ui->tab_anim);
    const char *title = L(S_NO_GAMES);
    const char *hint = on_root(L(S_HINT_GAMES));
    text_draw(MARGIN, 120, 0.9f, 0, with_alpha(TEXT_MAIN, a), ALIGN_LEFT, title);
    text_draw(MARGIN, 142, 0.62f, 0, with_alpha(TEXT_DIM, a), ALIGN_LEFT, hint);
}

void ui_draw(Ui *ui)
{
    gfx_begin(CLEAR_COLOR);
    draw_background(ui);
    /* Bibliotek, Musik, Bilder, Videor och Sök: mörkare yta än i raden – valt spels bild skymtar bara svagt.
     * Före toppraden, så att den är lika ljus i alla flikar. */
    if (ui->tab == TAB_LIBRARY && view(ui)->count)
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(8, 8, 12, 165));
    else if (ui->tab == TAB_MUSIC || ui->tab == TAB_PICTURES || ui->tab == TAB_VIDEO || ui->tab == TAB_SEARCH)
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(8, 8, 12, 150));
    draw_top_bar(ui);
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
        draw_row(ui);
        draw_details(ui);
    } else {
        draw_empty(ui);
    }
    draw_hints(ui);
    if (ui->tab == TAB_PICTURES && ui->pic_viewing)
        draw_pic_fullscreen(ui);
    if (ui->tab == TAB_VIDEO && ui->vid_viewing)
        draw_vid_fullscreen(ui);
    if (ui->tab == TAB_GAMES || ui->tab == TAB_LIBRARY)
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

void ui_draw_scanning(int done, const char *name, float time)
{
    static const float neutral[3] = { 120, 120, 120 };
    gfx_begin(CLEAR_COLOR);
    draw_spotlight(neutral, time);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, 90)); /* lugnare bakgrund bakom loggan */

    /* loggan i mitten, en tunn list där en ljus bit glider fram och tillbaka, texten under */
    float cx = SCREEN_W / 2;
    logo_draw(cx, 108, RGBA(236, 236, 240, 255));
    float bw = 120, bx = cx - bw / 2, by = 146, seg = 34;
    gfx_round_rect(bx, by, bw, 2, 1, RGBA(255, 255, 255, 38));
    float p = 0.5f - 0.5f * cosf(time * 2.4f);
    gfx_round_rect(bx + p * (bw - seg), by, seg, 2, 1, COLOR_ALPHA(theme_cur()->accent, 255));
    text_draw(cx, 172, 0.56f, 0, TEXT_DIM, ALIGN_CENTER, L(S_LOADING));
    char line[96], fit[96];
    snprintf(line, sizeof(line), "%d  ·  %.60s", done, name ? name : "");
    text_ellipsis(fit, sizeof(fit), line, 0.46f, 300);
    text_draw(cx, 188, 0.46f, 0, COLOR_ALPHA(TEXT_DIM, 150), ALIGN_CENTER, fit);

    /* tonas in från startskärmens svarta slut */
    float in = clampf(time / 0.4f, 0, 1);
    if (in < 1)
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(255 * (1 - in))));
    gfx_end();
}
