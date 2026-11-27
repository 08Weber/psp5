/* PSP5 REMAKE – Saved Data Manager (see savedata.h) */
#include <pspkernel.h>
#include <pspctrl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "savedata.h"
#include "art.h"
#include "datetime.h"
#include "gfx.h"
#include "lang.h"
#include "osd.h"
#include "sfo.h"
#include "sound.h"
#include "system.h"
#include "theme.h"

#define MAX_SFO_SIZE   (64 * 1024)
#define MAX_ICON_SIZE  (512 * 1024)
#define MAX_WALK_DEPTH 12
#define MAX_PATH_LEN   512
#define ICON_CACHE_CAP 10

#define LIST_X         16.0f
#define LIST_Y         56.0f
#define LIST_W         242.0f
#define LIST_H         180.0f
#define LIST_ROWS      5
#define ROW_H          34.0f
#define PREV_X         266.0f
#define PREV_Y         56.0f
#define PREV_W         198.0f
#define PREV_H         180.0f

#define DET_LEFT_X     20.0f
#define DET_LEFT_W     144.0f
#define DET_RIGHT_X    174.0f
#define DET_RIGHT_Y    56.0f
#define DET_RIGHT_W    290.0f
#define DET_RIGHT_H    180.0f
#define DET_ROW_H      24.0f
#define DET_ROWS       7

#define TEXT_MAIN      RGBA(238, 238, 242, 255)
#define TEXT_DIM       RGBA(160, 162, 172, 255)
#define LIGHT          RGBA(236, 236, 240, 255)
#define DARK           RGBA(18, 22, 30, 255)
#define PANEL          RGBA(32, 35, 44, 245)
#define DANGER         RGBA(240, 108, 98, 255)
#define WARN_COL       RGBA(242, 186, 92, 255)

typedef struct {
    char folder[128];
    char path[256];
    char game_title[128];
    char save_title[128];
    char detail[256];
    char sfo_dir[64];
    char game_id[16];
    u32 mtime;
    u64 size_bytes;
    u32 file_count;
    u8 storage;      /* SAVE_DEV_MS0 / SAVE_DEV_EF0 */
    u8 status;       /* SAVE_OK / SAVE_INCOMPLETE / SAVE_CORRUPTED / SAVE_PROTECTED */
    u8 has_icon;
    u8 icon_bad;
    u8 is_protected;
    u8 deleted;
    volatile u8 measured; /* 0 = pending, 1 = done */
} SaveEntry;

typedef struct {
    int all_idx;
    u32 stamp;
    Texture *tex;
} IconCacheSlot;

typedef struct {
    char label[40];
    char value[160];
    u8 is_status;
} DetailRow;

enum { MODE_LIST, MODE_DETAILS, MODE_MENU, MODE_CONFIRM_DEL, MODE_PROTECTED_WARN };
enum { MA_DETAILS, MA_DELETE, MA_SORT, MA_TOGGLE_FILTER, MA_STORAGE };

static SaveEntry *s_all;
static int s_all_n, s_all_cap;
static int *s_view;
static int s_view_n;

static IconCacheSlot s_icon_cache[ICON_CACHE_CAP];
static u32 s_icon_stamp;

static struct {
    int mode;
    int prev_mode; /* MODE_LIST or MODE_DETAILS when opening confirm/warn */
    int confirm_circle;
    int sel, top;
    float scroll, open, time;
    float icon_wait;
    int last_sel_all;
    int repeat_dir;
    float repeat_timer;

    int sort_mode;
    int storage_filter; /* 0 = All, 1 = ms0, 2 = ef0 */
    int has_ms0, has_ef0;

    int has_game_filter;
    int filter_active;
    char filter_id[16];
    char filter_title[128];

    int menu_sel, menu_n, menu_act[6];
    float menu_anim;

    int del_sel; /* 0 = Delete, 1 = Cancel (default) */
    int target_all_idx;

    char toast[96];
    float toast_time;

    /* Details view */
    DetailRow det_rows[20];
    int det_n, det_sel, det_top;
    float det_scroll;
    int det_all_idx;
} S;

/* --- Background size measurement thread --- */
static struct {
    SceUID thread;
    volatile int cancel;
    volatile int running;
    volatile int prio_idx;
    volatile int updated;
    u64 walk_bytes;
    u32 walk_files;
} BG = { .thread = -1, .prio_idx = -1 };

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static u32 fade(u32 c, float a) { return COLOR_ALPHA(c, (int)(((c >> 24) & 0xFF) * clampf(a, 0, 1))); }
static float approach(float cur, float target, float speed, float dt) { return cur + (target - cur) * (1.0f - expf(-speed * dt)); }

static void show_toast(const char *msg)
{
    snprintf(S.toast, sizeof(S.toast), "%s", msg);
    S.toast_time = 2.6f;
}

static void fmt_size(char *out, int size, u64 b)
{
    if (b >= 1024ULL * 1024 * 1024)
        snprintf(out, size, "%.1f GB", (float)(b / (1024.0f * 1024.0f)) / 1024.0f);
    else if (b >= 1024ULL * 1024)
        snprintf(out, size, "%.1f MB", (float)b / (1024.0f * 1024.0f));
    else if (b >= 1024)
        snprintf(out, size, "%u KB", (unsigned)(b / 1024));
    else
        snprintf(out, size, "%u B", (unsigned)b);
}

static u32 pack_time_dt(const ScePspDateTime *t)
{
    if (!t || t->year < 1980 || t->year > 2043 || t->month < 1 || t->month > 12 || t->day < 1 || t->day > 31)
        return 0;
    return dt_pack(t->year, t->month, t->day, t->hour, t->minute, t->second);
}

static void strip_marks(char *s)
{
    static const char *marks[] = { "\xE2\x84\xA2", "\xC2\xAE" };
    for (int m = 0; m < 2; m++) {
        int len = strlen(marks[m]);
        char *p;
        while ((p = strstr(s, marks[m])))
            memmove(p, p + len, strlen(p + len) + 1);
    }
    int n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
}

/* ---------------------------------------------------------------------- */
/* Path safety & ARK_01234 protection */

static int valid_folder_name(const char *name)
{
    if (!name || !name[0] || !strcmp(name, ".") || !strcmp(name, ".."))
        return 0;
    if (strstr(name, "..") || strpbrk(name, "/\\:"))
        return 0;
    return 1;
}

/* Ensures `path` is strictly "<ms0:|ef0:>/PSP/SAVEDATA/<single_folder>" */
static int is_allowed_save_path(const char *path, char *out_folder, int folder_size)
{
    if (!path)
        return 0;
    const char *sub = NULL;
    if (!strncasecmp(path, "ms0:/PSP/SAVEDATA/", 18))
        sub = path + 18;
    else if (!strncasecmp(path, "ef0:/PSP/SAVEDATA/", 18))
        sub = path + 18;
    else
        return 0;
    if (!valid_folder_name(sub))
        return 0;
    if (out_folder && folder_size > 0)
        snprintf(out_folder, folder_size, "%s", sub);
    return 1;
}

static int is_ark_protected(const char *path, const char *folder)
{
    if (!folder || !folder[0])
        return 1;
    if (!strcasecmp(folder, "ARK_01234"))
        return 1;

    const char *ark_dir = sys_ark_dir();
    if (ark_dir && ark_dir[0]) {
        char norm_ark[256], norm_path[256];
        snprintf(norm_ark, sizeof(norm_ark), "%s", ark_dir);
        int alen = strlen(norm_ark);
        while (alen > 0 && (norm_ark[alen - 1] == '/' || norm_ark[alen - 1] == '\\'))
            norm_ark[--alen] = 0;

        if (path && path[0]) {
            snprintf(norm_path, sizeof(norm_path), "%s", path);
            int plen = strlen(norm_path);
            while (plen > 0 && (norm_path[plen - 1] == '/' || norm_path[plen - 1] == '\\'))
                norm_path[--plen] = 0;
            if (!strcasecmp(norm_path, norm_ark))
                return 1;
        }

        /* Also compare the folder component if sys_ark_dir() resides inside PSP/SAVEDATA/ */
        for (const char *p = norm_ark; *p; p++) {
            if (!strncasecmp(p, "/PSP/SAVEDATA/", 14)) {
                const char *af = p + 14;
                if (*af && !strchr(af, '/') && !strcasecmp(folder, af))
                    return 1;
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Background folder size & file count worker */

static void measure_walk(const char *path, int depth)
{
    if (depth > MAX_WALK_DEPTH || BG.cancel)
        return;
    SceUID d = sceIoDopen(path);
    if (d < 0)
        return;

    typedef struct {
        char name[256];
        u64 size;
        u8 is_dir;
    } WalkItem;

    WalkItem *items = NULL;
    int n = 0, cap = 0;
    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));
    while (!BG.cancel && sceIoDread(d, &ent) > 0) {
        if (strcmp(ent.d_name, ".") && strcmp(ent.d_name, "..") && !strpbrk(ent.d_name, "/\\")) {
            if (n == cap) {
                cap = cap ? cap * 2 : 16;
                WalkItem *grown = realloc(items, (size_t)cap * sizeof(*items));
                if (!grown)
                    break;
                items = grown;
            }
            WalkItem *it = &items[n++];
            snprintf(it->name, sizeof(it->name), "%s", ent.d_name);
            it->is_dir = FIO_S_ISDIR(ent.d_stat.st_mode) != 0;
            it->size = it->is_dir ? 0 : (u64)ent.d_stat.st_size;
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(d);

    for (int i = 0; i < n && !BG.cancel; i++) {
        if (items[i].is_dir) {
            char child[MAX_PATH_LEN];
            snprintf(child, sizeof(child), "%s/%s", path, items[i].name);
            measure_walk(child, depth + 1);
        } else {
            BG.walk_bytes += items[i].size;
            BG.walk_files++;
        }
    }
    free(items);
}

static int measure_thread(SceSize args, void *argp)
{
    while (!BG.cancel) {
        int idx = BG.prio_idx;
        if (idx < 0 || idx >= s_all_n || s_all[idx].deleted || s_all[idx].measured) {
            idx = -1;
            for (int i = 0; i < s_all_n; i++) {
                if (!s_all[i].deleted && !s_all[i].measured) {
                    idx = i;
                    break;
                }
            }
        }
        if (idx < 0)
            break;

        BG.walk_bytes = 0;
        BG.walk_files = 0;
        measure_walk(s_all[idx].path, 0);
        if (BG.cancel)
            break;

        s_all[idx].size_bytes = BG.walk_bytes;
        s_all[idx].file_count = BG.walk_files;
        if (s_all[idx].status == SAVE_OK && BG.walk_files == 0)
            s_all[idx].status = SAVE_INCOMPLETE;
        __asm__ volatile("" ::: "memory");
        s_all[idx].measured = 1;
        BG.updated = 1;
    }
    BG.running = 0;
    return 0;
}

static void measure_stop(void)
{
    if (BG.thread >= 0) {
        BG.cancel = 1;
        sceKernelWaitThreadEnd(BG.thread, NULL);
        sceKernelDeleteThread(BG.thread);
        BG.thread = -1;
    }
    BG.running = 0;
}

static void measure_start(void)
{
    measure_stop();
    int any = 0;
    for (int i = 0; i < s_all_n; i++) {
        if (!s_all[i].deleted && !s_all[i].measured) {
            any = 1;
            break;
        }
    }
    if (!any)
        return;
    BG.cancel = 0;
    BG.running = 1;
    BG.thread = sceKernelCreateThread("psp5_savesz", measure_thread, 0x30, 0x8000, PSP_THREAD_ATTR_USER, NULL);
    if (BG.thread >= 0 && sceKernelStartThread(BG.thread, 0, NULL) < 0) {
        sceKernelDeleteThread(BG.thread);
        BG.thread = -1;
    }
    if (BG.thread < 0)
        BG.running = 0;
}

/* ---------------------------------------------------------------------- */
/* Safe deletion */

static int delete_tree_safe(const char *path, int depth)
{
    if (depth > MAX_WALK_DEPTH)
        return 0;
    SceUID d = sceIoDopen(path);
    if (d < 0)
        return 0;

    typedef struct {
        char name[256];
        u8 is_dir;
    } DelItem;

    DelItem *items = NULL;
    int n = 0, cap = 0;
    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(d, &ent) > 0) {
        if (strcmp(ent.d_name, ".") && strcmp(ent.d_name, "..")) {
            if (!valid_folder_name(ent.d_name)) {
                free(items);
                sceIoDclose(d);
                return 0;
            }
            if (n == cap) {
                cap = cap ? cap * 2 : 16;
                DelItem *grown = realloc(items, (size_t)cap * sizeof(*items));
                if (!grown) {
                    free(items);
                    sceIoDclose(d);
                    return 0;
                }
                items = grown;
            }
            snprintf(items[n].name, sizeof(items[n].name), "%s", ent.d_name);
            items[n].is_dir = FIO_S_ISDIR(ent.d_stat.st_mode) != 0;
            n++;
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(d);

    int ok = 1;
    for (int i = 0; i < n && ok; i++) {
        char child[MAX_PATH_LEN];
        snprintf(child, sizeof(child), "%s/%s", path, items[i].name);
        if (items[i].is_dir)
            ok = delete_tree_safe(child, depth + 1);
        else if (sceIoRemove(child) < 0)
            ok = 0;
    }
    free(items);
    if (ok && sceIoRmdir(path) < 0)
        ok = 0;
    return ok;
}

int savedata_delete_folder(const char *path)
{
    char folder[128];
    if (!is_allowed_save_path(path, folder, sizeof(folder)))
        return SAVE_DEL_INVALID;
    if (is_ark_protected(path, folder))
        return SAVE_DEL_PROTECTED;

    SceIoStat st;
    memset(&st, 0, sizeof(st));
    if (sceIoGetstat(path, &st) < 0 || !FIO_S_ISDIR(st.st_mode))
        return SAVE_DEL_FAILED;

    measure_stop();
    if (!delete_tree_safe(path, 0))
        return SAVE_DEL_FAILED;
    return SAVE_DEL_OK;
}

/* ---------------------------------------------------------------------- */
/* Scanning & Game ID extraction */

static int is_upper_alpha(char c) { return c >= 'A' && c <= 'Z'; }
static int is_alpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_alnum_char(char c) { return is_alpha(c) || is_digit(c); }
static char to_upper_ascii(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static int looks_like_psp_id(const char *s)
{
    if (!s || strlen(s) < 9)
        return 0;
    for (int i = 0; i < 4; i++)
        if (!is_alpha(s[i]))
            return 0;
    for (int i = 4; i < 9; i++)
        if (!is_digit(s[i]))
            return 0;
    return 1;
}

static void extract_game_id(const u8 *sfo, u32 sfo_size, const char *sfo_dir, const char *folder, char *out, int out_size)
{
    out[0] = 0;
    char buf[32] = "";
    if (sfo && sfo_get_string(sfo, sfo_size, "DISC_ID", buf, sizeof(buf)) && buf[0]) {
        snprintf(out, out_size, "%s", buf);
        return;
    }
    const char *cands[2] = { sfo_dir, folder };
    for (int c = 0; c < 2; c++) {
        const char *s = cands[c];
        if (s && looks_like_psp_id(s) && out_size >= 10) {
            for (int i = 0; i < 9; i++)
                out[i] = to_upper_ascii(s[i]);
            out[9] = 0;
            return;
        }
    }
    if (sfo_dir && strlen(sfo_dir) >= 9 && out_size >= 10) {
        int ok = 1;
        for (int i = 0; i < 9; i++)
            if (!is_alnum_char(sfo_dir[i]))
                ok = 0;
        if (ok) {
            for (int i = 0; i < 9; i++)
                out[i] = to_upper_ascii(sfo_dir[i]);
            out[9] = 0;
        }
    }
    (void)is_upper_alpha;
}

static SaveEntry *add_save_entry(void)
{
    if (s_all_n == s_all_cap) {
        int cap = s_all_cap ? s_all_cap * 2 : 32;
        SaveEntry *grown = realloc(s_all, (size_t)cap * sizeof(*s_all));
        if (!grown)
            return NULL;
        s_all = grown;
        s_all_cap = cap;
    }
    SaveEntry *e = &s_all[s_all_n++];
    memset(e, 0, sizeof(*e));
    return e;
}

static void scan_savedata_dir(const char *root, u8 storage)
{
    SceUID d = sceIoDopen(root);
    if (d < 0)
        return;
    if (storage == SAVE_DEV_MS0)
        S.has_ms0 = 1;
    else
        S.has_ef0 = 1;

    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(d, &ent) > 0) {
        if (strcmp(ent.d_name, ".") && strcmp(ent.d_name, "..") &&
            FIO_S_ISDIR(ent.d_stat.st_mode) && valid_folder_name(ent.d_name)) {
            SaveEntry *e = add_save_entry();
            if (!e)
                break;
            snprintf(e->folder, sizeof(e->folder), "%s", ent.d_name);
            snprintf(e->path, sizeof(e->path), "%s/%s", root, ent.d_name);
            e->storage = storage;
            e->mtime = pack_time_dt(&ent.d_stat.sce_st_mtime);
            e->is_protected = is_ark_protected(e->path, e->folder) ? 1 : 0;

            char sfo_path[300], icon_path[300];
            snprintf(sfo_path, sizeof(sfo_path), "%s/PARAM.SFO", e->path);
            snprintf(icon_path, sizeof(icon_path), "%s/ICON0.PNG", e->path);

            SceIoStat st;
            memset(&st, 0, sizeof(st));
            if (sceIoGetstat(sfo_path, &st) >= 0) {
                u32 mt = pack_time_dt(&st.sce_st_mtime);
                if (mt)
                    e->mtime = mt;
                if (st.st_size > 0 && st.st_size <= MAX_SFO_SIZE) {
                    u8 *sfo = malloc((size_t)st.st_size);
                    SceUID fd = sfo ? sceIoOpen(sfo_path, PSP_O_RDONLY, 0) : -1;
                    int got = (fd >= 0) ? sceIoRead(fd, sfo, (u32)st.st_size) : -1;
                    if (fd >= 0)
                        sceIoClose(fd);
                    if (sfo && got == (int)st.st_size && sfo_count(sfo, (u32)got) > 0) {
                        sfo_get_string(sfo, (u32)got, "TITLE", e->game_title, sizeof(e->game_title));
                        strip_marks(e->game_title);
                        sfo_get_string(sfo, (u32)got, "SAVEDATA_TITLE", e->save_title, sizeof(e->save_title));
                        strip_marks(e->save_title);
                        sfo_get_string(sfo, (u32)got, "SAVEDATA_DETAIL", e->detail, sizeof(e->detail));
                        strip_marks(e->detail);
                        sfo_get_string(sfo, (u32)got, "SAVEDATA_DIRECTORY", e->sfo_dir, sizeof(e->sfo_dir));
                        extract_game_id(sfo, (u32)got, e->sfo_dir, e->folder, e->game_id, sizeof(e->game_id));
                        if (!e->game_title[0] && !e->save_title[0]) {
                            snprintf(e->game_title, sizeof(e->game_title), "%s", e->folder);
                            e->status = e->is_protected ? SAVE_PROTECTED : SAVE_INCOMPLETE;
                        } else {
                            if (!e->game_title[0])
                                snprintf(e->game_title, sizeof(e->game_title), "%s", e->folder);
                            e->status = e->is_protected ? SAVE_PROTECTED : SAVE_OK;
                        }
                    } else {
                        snprintf(e->game_title, sizeof(e->game_title), "%s", e->folder);
                        extract_game_id(NULL, 0, "", e->folder, e->game_id, sizeof(e->game_id));
                        e->status = e->is_protected ? SAVE_PROTECTED : SAVE_CORRUPTED;
                    }
                    free(sfo);
                } else {
                    snprintf(e->game_title, sizeof(e->game_title), "%s", e->folder);
                    extract_game_id(NULL, 0, "", e->folder, e->game_id, sizeof(e->game_id));
                    e->status = e->is_protected ? SAVE_PROTECTED : SAVE_CORRUPTED;
                }
            } else {
                snprintf(e->game_title, sizeof(e->game_title), "%s", e->folder);
                extract_game_id(NULL, 0, "", e->folder, e->game_id, sizeof(e->game_id));
                e->status = e->is_protected ? SAVE_PROTECTED : SAVE_INCOMPLETE;
            }

            memset(&st, 0, sizeof(st));
            if (sceIoGetstat(icon_path, &st) >= 0 && st.st_size > 0 && st.st_size <= MAX_ICON_SIZE)
                e->has_icon = 1;
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(d);
}

static void scan_all_savedata(void)
{
    free(s_all);
    s_all = NULL;
    s_all_n = s_all_cap = 0;
    S.has_ms0 = S.has_ef0 = 0;

    if (!sys_ms_is_ef())
        scan_savedata_dir("ms0:/PSP/SAVEDATA", SAVE_DEV_MS0);
    scan_savedata_dir("ef0:/PSP/SAVEDATA", SAVE_DEV_EF0);

    int cjk = 0;
    for (int i = 0; i < s_all_n && !cjk; i++) {
        const char *fields[3] = { s_all[i].game_title, s_all[i].save_title, s_all[i].detail };
        for (int f = 0; f < 3 && !cjk; f++)
            for (const char *p = fields[f]; *p; p++)
                if ((u8)*p >= 0x80) {
                    cjk = 1;
                    break;
                }
    }
    if (cjk)
        font_ensure_cjk();
}

/* ---------------------------------------------------------------------- */
/* Filtering & Sorting */

static int matches_game_filter(const SaveEntry *e, const char *game_id)
{
    if (!game_id || !game_id[0])
        return 1;
    if (e->is_protected)
        return 0;
    if (e->game_id[0] && !strcasecmp(e->game_id, game_id))
        return 1;
    size_t len = strlen(game_id);
    if (len >= 4) {
        if (!strncasecmp(e->folder, game_id, len))
            return 1;
        if (e->sfo_dir[0] && !strncasecmp(e->sfo_dir, game_id, len))
            return 1;
    }
    return 0;
}

static int cmp_save_idx(const void *a, const void *b)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    const SaveEntry *x = &s_all[ia];
    const SaveEntry *y = &s_all[ib];

    if (S.sort_mode == SAVE_SORT_DATE) {
        if (x->mtime != y->mtime)
            return x->mtime > y->mtime ? -1 : 1;
    } else if (S.sort_mode == SAVE_SORT_SIZE) {
        if (x->measured != y->measured)
            return x->measured ? -1 : 1;
        if (x->measured && y->measured && x->size_bytes != y->size_bytes)
            return x->size_bytes > y->size_bytes ? -1 : 1;
    }

    int c = strcasecmp(x->game_title, y->game_title);
    if (c)
        return c;
    c = strcasecmp(x->save_title, y->save_title);
    if (c)
        return c;
    return strcasecmp(x->folder, y->folder);
}

static void rebuild_view(int keep_all_idx, int fallback_sel)
{
    free(s_view);
    s_view = s_all_n ? malloc((size_t)s_all_n * sizeof(int)) : NULL;
    s_view_n = 0;

    if (s_view) {
        for (int i = 0; i < s_all_n; i++) {
            const SaveEntry *e = &s_all[i];
            if (e->deleted)
                continue;
            if (S.storage_filter == 1 && e->storage != SAVE_DEV_MS0)
                continue;
            if (S.storage_filter == 2 && e->storage != SAVE_DEV_EF0)
                continue;
            if (S.filter_active && !matches_game_filter(e, S.filter_id))
                continue;
            s_view[s_view_n++] = i;
        }
        if (s_view_n > 1)
            qsort(s_view, s_view_n, sizeof(int), cmp_save_idx);
    }

    int new_sel = fallback_sel < s_view_n ? fallback_sel : s_view_n - 1;
    if (new_sel < 0)
        new_sel = 0;
    if (keep_all_idx >= 0) {
        for (int i = 0; i < s_view_n; i++) {
            if (s_view[i] == keep_all_idx) {
                new_sel = i;
                break;
            }
        }
    }
    S.sel = new_sel;
    S.top = S.sel - LIST_ROWS / 2;
    if (S.top > s_view_n - LIST_ROWS)
        S.top = s_view_n - LIST_ROWS;
    if (S.top < 0)
        S.top = 0;
    S.scroll = S.top * ROW_H;
    BG.prio_idx = s_view_n ? s_view[S.sel] : -1;
}

/* ---------------------------------------------------------------------- */
/* Bounded LRU Icon Cache */

static void icon_cache_clear(void)
{
    for (int i = 0; i < ICON_CACHE_CAP; i++) {
        tex_free(s_icon_cache[i].tex);
        s_icon_cache[i].tex = NULL;
        s_icon_cache[i].all_idx = -1;
        s_icon_cache[i].stamp = 0;
    }
    s_icon_stamp = 0;
}

static void icon_cache_evict_idx(int all_idx)
{
    for (int i = 0; i < ICON_CACHE_CAP; i++) {
        if (s_icon_cache[i].all_idx == all_idx) {
            tex_free(s_icon_cache[i].tex);
            s_icon_cache[i].tex = NULL;
            s_icon_cache[i].all_idx = -1;
            s_icon_cache[i].stamp = 0;
        }
    }
}

static Texture *icon_cache_peek(int all_idx)
{
    if (all_idx < 0 || all_idx >= s_all_n)
        return NULL;
    for (int i = 0; i < ICON_CACHE_CAP; i++) {
        if (s_icon_cache[i].all_idx == all_idx && s_icon_cache[i].tex) {
            s_icon_cache[i].stamp = ++s_icon_stamp;
            return s_icon_cache[i].tex;
        }
    }
    return NULL;
}

static Texture *icon_cache_get(int all_idx)
{
    Texture *t = icon_cache_peek(all_idx);
    if (t)
        return t;
    if (all_idx < 0 || all_idx >= s_all_n)
        return NULL;
    SaveEntry *e = &s_all[all_idx];
    if (e->deleted || !e->has_icon || e->icon_bad)
        return NULL;

    char icon_path[300];
    snprintf(icon_path, sizeof(icon_path), "%s/ICON0.PNG", e->path);
    t = art_load_icon_file(icon_path);
    if (!t) {
        e->icon_bad = 1;
        return NULL;
    }

    int slot = -1;
    u32 oldest = 0xFFFFFFFFu;
    for (int i = 0; i < ICON_CACHE_CAP; i++) {
        if (!s_icon_cache[i].tex) {
            slot = i;
            break;
        }
        if (s_icon_cache[i].stamp < oldest) {
            oldest = s_icon_cache[i].stamp;
            slot = i;
        }
    }
    if (slot >= 0) {
        tex_free(s_icon_cache[slot].tex);
        s_icon_cache[slot].tex = t;
        s_icon_cache[slot].all_idx = all_idx;
        s_icon_cache[slot].stamp = ++s_icon_stamp;
    }
    return t;
}

/* ---------------------------------------------------------------------- */
/* Details View builder */

static const char *status_text(u8 status)
{
    switch (status) {
    case SAVE_PROTECTED:  return L(S_SD_PROTECTED);
    case SAVE_INCOMPLETE: return L(S_SD_INCOMPLETE);
    case SAVE_CORRUPTED:  return L(S_SD_CORRUPTED);
    default:              return L(S_SD_OK);
    }
}

static u32 status_color(const Theme *th, u8 status)
{
    switch (status) {
    case SAVE_PROTECTED:  return THEME_INK(th, warn, WARN_COL);
    case SAVE_INCOMPLETE: return THEME_INK(th, warn, WARN_COL);
    case SAVE_CORRUPTED:  return THEME_INK(th, err, DANGER);
    default:              return THEME_INK(th, ok, RGBA(110, 212, 146, 255));
    }
}

static void add_det_row(const char *label, const char *value, u8 is_status)
{
    if (S.det_n >= (int)(sizeof(S.det_rows) / sizeof(S.det_rows[0])))
        return;
    DetailRow *r = &S.det_rows[S.det_n++];
    snprintf(r->label, sizeof(r->label), "%s", label);
    snprintf(r->value, sizeof(r->value), "%s", value && value[0] ? value : "-");
    r->is_status = is_status;
}

static void build_details(int all_idx)
{
    S.det_n = 0;
    S.det_all_idx = all_idx;
    if (all_idx < 0 || all_idx >= s_all_n)
        return;
    const SaveEntry *e = &s_all[all_idx];

    add_det_row(L(S_SD_SAVE_TITLE), e->save_title, 0);
    add_det_row(L(S_SD_GAME_ID), e->game_id, 0);
    add_det_row(L(S_I_FOLDER), e->folder, 0);
    add_det_row(L(S_I_LOCATION), L(e->storage == SAVE_DEV_EF0 ? S_F_INTERNAL : S_F_MS), 0);

    char date_buf[48] = "-";
    if (e->mtime)
        dt_text_stored(date_buf, sizeof(date_buf), e->mtime);
    add_det_row(L(S_I_MODIFIED), date_buf, 0);

    char sz_buf[32], fc_buf[24];
    if (e->measured) {
        fmt_size(sz_buf, sizeof(sz_buf), e->size_bytes);
        snprintf(fc_buf, sizeof(fc_buf), "%u", (unsigned)e->file_count);
    } else {
        snprintf(sz_buf, sizeof(sz_buf), "%s", L(S_SD_CALCULATING));
        snprintf(fc_buf, sizeof(fc_buf), "%s", L(S_SD_CALCULATING));
    }
    add_det_row(L(S_I_SIZE), sz_buf, 0);
    add_det_row(L(S_SD_FILES), fc_buf, 0);
    add_det_row(L(S_SD_STATUS), status_text(e->status), 1);

    if (e->detail[0]) {
        char lines[4][96];
        int nl = text_wrap(e->detail, 0.50f, DET_RIGHT_W - 110.0f, lines, 4);
        for (int i = 0; i < nl; i++)
            add_det_row(i == 0 ? L(S_SD_DESC) : "", lines[i], 0);
    } else {
        add_det_row(L(S_SD_DESC), "-", 0);
    }

    if (S.det_sel >= S.det_n)
        S.det_sel = S.det_n > 0 ? S.det_n - 1 : 0;
}

/* ---------------------------------------------------------------------- */
/* Lifecycle & Actions */

void savedata_open(const char *filter_game_id, const char *filter_game_title, int confirm_circle)
{
    measure_stop();
    icon_cache_clear();
    memset(&S, 0, sizeof(S));
    S.confirm_circle = confirm_circle;
    S.last_sel_all = -1;
    S.target_all_idx = -1;
    S.det_all_idx = -1;
    S.del_sel = 1; /* Cancel is default */
    S.sort_mode = SAVE_SORT_DATE;

    if (filter_game_id && filter_game_id[0]) {
        S.has_game_filter = 1;
        S.filter_active = 1;
        snprintf(S.filter_id, sizeof(S.filter_id), "%s", filter_game_id);
        snprintf(S.filter_title, sizeof(S.filter_title), "%s",
                 (filter_game_title && filter_game_title[0]) ? filter_game_title : filter_game_id);
    }

    scan_all_savedata();
    rebuild_view(-1, 0);
    S.last_sel_all = s_view_n ? s_view[S.sel] : -1;
    measure_start();
}

void savedata_close(void)
{
    measure_stop();
    icon_cache_clear();
    free(s_view);
    s_view = NULL;
    s_view_n = 0;
    free(s_all);
    s_all = NULL;
    s_all_n = s_all_cap = 0;
}

static void open_menu(void)
{
    S.menu_n = 0;
    S.menu_sel = 0;
    if (s_view_n > 0) {
        S.menu_act[S.menu_n++] = MA_DETAILS;
        S.menu_act[S.menu_n++] = MA_DELETE;
    }
    S.menu_act[S.menu_n++] = MA_SORT;
    if (S.has_game_filter)
        S.menu_act[S.menu_n++] = MA_TOGGLE_FILTER;
    if (S.has_ms0 && S.has_ef0)
        S.menu_act[S.menu_n++] = MA_STORAGE;
    S.mode = MODE_MENU;
    sound_play(SND_OPEN);
}

static void request_delete(int all_idx, int from_mode)
{
    if (all_idx < 0 || all_idx >= s_all_n || s_all[all_idx].deleted)
        return;
    S.target_all_idx = all_idx;
    S.prev_mode = from_mode;
    const SaveEntry *e = &s_all[all_idx];
    if (e->is_protected || is_ark_protected(e->path, e->folder)) {
        S.mode = MODE_PROTECTED_WARN;
        sound_play(SND_ERROR);
        return;
    }
    S.del_sel = 1; /* Cancel is always default */
    S.mode = MODE_CONFIRM_DEL;
    sound_play(SND_OPEN);
}

static void cycle_sort(void)
{
    int cur = s_view_n ? s_view[S.sel] : -1;
    S.sort_mode = (S.sort_mode + 1) % SAVE_SORT_COUNT;
    rebuild_view(cur, S.sel);
    sound_play(SND_SLIDER_UP);
}

static void toggle_filter(void)
{
    if (!S.has_game_filter)
        return;
    int cur = s_view_n ? s_view[S.sel] : -1;
    S.filter_active = !S.filter_active;
    rebuild_view(cur, 0);
    sound_play(SND_TOGGLE);
}

static void cycle_storage(void)
{
    if (!(S.has_ms0 && S.has_ef0))
        return;
    int cur = s_view_n ? s_view[S.sel] : -1;
    S.storage_filter = (S.storage_filter + 1) % 3;
    rebuild_view(cur, 0);
    sound_play(SND_SLIDER_UP);
}

static void execute_delete(void)
{
    int idx = S.target_all_idx;
    if (idx < 0 || idx >= s_all_n) {
        S.mode = S.prev_mode;
        return;
    }
    SaveEntry *e = &s_all[idx];
    int res = savedata_delete_folder(e->path);
    if (res == SAVE_DEL_PROTECTED) {
        S.mode = MODE_PROTECTED_WARN;
        sound_play(SND_ERROR);
        return;
    }
    if (res == SAVE_DEL_OK) {
        e->deleted = 1;
        icon_cache_evict_idx(idx);
        int old_sel = S.sel;
        rebuild_view(-1, old_sel);
        measure_start();
        S.mode = MODE_LIST;
        show_toast(L(S_SD_DELETED));
        sound_play(SND_TOAST);
    } else {
        S.mode = S.prev_mode;
        measure_start();
        show_toast(L(S_SD_DEL_FAILED));
        sound_play(SND_ERROR);
    }
}

/* ---------------------------------------------------------------------- */
/* Input & Update */

static int vertical(const UiInput *in, float dt)
{
    int dir = ((in->held & PSP_CTRL_UP) || in->ly < 40) ? -1 : ((in->held & PSP_CTRL_DOWN) || in->ly > 215) ? 1 : 0;
    if (!dir) {
        S.repeat_dir = 0;
        return 0;
    }
    if (dir != S.repeat_dir) {
        S.repeat_dir = dir;
        S.repeat_timer = 0.32f;
        return dir;
    }
    S.repeat_timer -= dt;
    if (S.repeat_timer > 0)
        return 0;
    S.repeat_timer += 0.07f;
    return dir;
}

int savedata_update(const UiInput *in, float dt)
{
    S.open = clampf(S.open + dt * 5.0f, 0, 1);
    S.time += dt;
    S.menu_anim = clampf(S.menu_anim + (S.mode == MODE_MENU ? 7.0f : -7.0f) * dt, 0, 1);
    if (S.toast_time > 0)
        S.toast_time -= dt;

    if (BG.updated) {
        BG.updated = 0;
        if (S.sort_mode == SAVE_SORT_SIZE && S.mode == MODE_LIST && s_view_n > 1) {
            int cur = s_view[S.sel];
            rebuild_view(cur, S.sel);
        }
        if (S.mode == MODE_DETAILS && S.det_all_idx >= 0)
            build_details(S.det_all_idx);
    }

    u32 ok = S.confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
    u32 back = S.confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;
    int move = vertical(in, dt);

    if (S.mode == MODE_PROTECTED_WARN) {
        if (in->pressed & (ok | back)) {
            S.mode = S.prev_mode;
            sound_play(SND_BACK);
        }
        return 0;
    }

    if (S.mode == MODE_CONFIRM_DEL) {
        if (move || (in->pressed & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT))) {
            S.del_sel = !S.del_sel;
            sound_play(SND_MOVE);
        } else if (in->pressed & ok) {
            if (S.del_sel == 0)
                execute_delete();
            else {
                S.mode = S.prev_mode;
                sound_play(SND_CLOSE);
            }
        } else if (in->pressed & back) {
            S.mode = S.prev_mode;
            sound_play(SND_CLOSE);
        }
        return 0;
    }

    if (S.mode == MODE_MENU) {
        if (move && S.menu_n > 0) {
            int next = (S.menu_sel + move + S.menu_n) % S.menu_n;
            if (next != S.menu_sel)
                sound_play(SND_MOVE);
            S.menu_sel = next;
        } else if (in->pressed & ok) {
            int act = S.menu_act[S.menu_sel];
            S.mode = MODE_LIST;
            if (act == MA_DETAILS && s_view_n > 0) {
                S.det_sel = 0;
                S.det_top = 0;
                S.det_scroll = 0;
                build_details(s_view[S.sel]);
                S.mode = MODE_DETAILS;
                sound_play(SND_DETAIL_IN);
            } else if (act == MA_DELETE && s_view_n > 0) {
                request_delete(s_view[S.sel], MODE_LIST);
            } else if (act == MA_SORT) {
                cycle_sort();
            } else if (act == MA_TOGGLE_FILTER) {
                toggle_filter();
            } else if (act == MA_STORAGE) {
                cycle_storage();
            }
        } else if (in->pressed & (back | PSP_CTRL_TRIANGLE)) {
            S.mode = MODE_LIST;
            sound_play(SND_CLOSE);
        }
        return 0;
    }

    if (S.mode == MODE_DETAILS) {
        if (move && S.det_n > 0) {
            int next = (S.det_sel + move + S.det_n) % S.det_n;
            if (next != S.det_sel)
                sound_play(SND_MOVE);
            S.det_sel = next;
        }
        if (S.det_sel < S.det_top)
            S.det_top = S.det_sel;
        if (S.det_sel > S.det_top + DET_ROWS - 1)
            S.det_top = S.det_sel - (DET_ROWS - 1);
        S.det_scroll = approach(S.det_scroll, S.det_top * DET_ROW_H, 14.0f, dt);

        if (in->pressed & PSP_CTRL_TRIANGLE) {
            request_delete(S.det_all_idx, MODE_DETAILS);
        } else if (S.open >= 1.0f && (in->pressed & back)) {
            S.mode = MODE_LIST;
            sound_play(SND_DETAIL_OUT);
        }
        return 0;
    }

    /* MODE_LIST */
    int page = 0;
    if (in->pressed & PSP_CTRL_LTRIGGER)
        page = -LIST_ROWS;
    if (in->pressed & PSP_CTRL_RTRIGGER)
        page = LIST_ROWS;

    if ((move || page) && s_view_n > 0) {
        int next = page ? (int)clampf(S.sel + page, 0, s_view_n - 1) : (S.sel + move + s_view_n) % s_view_n;
        if (next != S.sel) {
            S.sel = next;
            BG.prio_idx = s_view[S.sel];
            sound_play(SND_MOVE);
        }
    }
    if (S.sel < S.top)
        S.top = S.sel;
    if (S.sel > S.top + LIST_ROWS - 1)
        S.top = S.sel - (LIST_ROWS - 1);
    S.scroll = approach(S.scroll, S.top * ROW_H, 14.0f, dt);

    int cur_all = s_view_n ? s_view[S.sel] : -1;
    if (cur_all != S.last_sel_all) {
        S.last_sel_all = cur_all;
        S.icon_wait = 0.05f;
    } else if (S.icon_wait > 0) {
        S.icon_wait -= dt;
    }

    if (S.open >= 1.0f && (in->pressed & back)) {
        sound_play(SND_DETAIL_OUT);
        return 1;
    }
    if (in->pressed & ok) {
        if (s_view_n > 0) {
            S.det_sel = 0;
            S.det_top = 0;
            S.det_scroll = 0;
            build_details(s_view[S.sel]);
            S.mode = MODE_DETAILS;
            sound_play(SND_DETAIL_IN);
        } else if (S.has_game_filter && S.filter_active) {
            toggle_filter();
        }
    } else if (in->pressed & PSP_CTRL_SQUARE) {
        if (S.has_game_filter && S.filter_active && s_view_n == 0)
            toggle_filter();
        else if (s_view_n > 0)
            cycle_sort();
    } else if (in->pressed & PSP_CTRL_SELECT) {
        if (S.has_game_filter)
            toggle_filter();
        else if (S.has_ms0 && S.has_ef0)
            cycle_storage();
    } else if (in->pressed & PSP_CTRL_TRIANGLE) {
        open_menu();
    }
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Drawing */

static const char *sort_label(void)
{
    switch (S.sort_mode) {
    case SAVE_SORT_NAME: return L(S_SORT_NAME);
    case SAVE_SORT_SIZE: return L(S_SD_SORT_SIZE);
    default:             return L(S_SD_SORT_DATE);
    }
}

static const char *storage_filter_label(void)
{
    if (S.storage_filter == 1)
        return L(S_F_MS);
    if (S.storage_filter == 2)
        return L(S_F_INTERNAL);
    if (S.has_ms0 && S.has_ef0)
        return L(S_SD_ALL_STORAGE);
    return S.has_ef0 ? L(S_F_INTERNAL) : L(S_F_MS);
}

static void draw_card(const Theme *th, float x, float y, float w, float h, float r, float a)
{
    int def = th == theme_get(0);
    u32 bg = th->ink ? COLOR_ALPHA(th->ink->panel, (int)(242 * a))
           : def     ? RGBA(16, 22, 32, (int)(232 * a))
                     : RGBA(14, 18, 26, (int)(208 * a));
    u32 br = th->ink ? COLOR_ALPHA(th->ink->border, (int)(235 * a))
           : def     ? RGBA(56, 72, 98, (int)(195 * a))
                     : COLOR_ALPHA(th->accent, (int)(135 * a));
    gfx_round_rect(x, y, w, h, r, bg);
    gfx_ring(x, y, w, h, r, br);
}

static void draw_top_header(const Theme *th, float a, float slide)
{
    u32 main_c = fade(THEME_INK(th, text, TEXT_MAIN), a);
    u32 dim_c = fade(THEME_INK(th, text_dim, TEXT_DIM), a);
    const char *title = L(S_SAVEDATA);
    text_draw(LIST_X + slide, 30.0f, 0.88f, 1, main_c, ALIGN_LEFT, title);

    float tx = LIST_X + slide + text_width(0.88f, 1, title) + 10.0f;
    char mode_lbl[72];
    if (S.filter_active)
        text_ellipsis(mode_lbl, sizeof(mode_lbl), S.filter_title, 0.48f, 110.0f);
    else
        snprintf(mode_lbl, sizeof(mode_lbl), "%s", L(S_ALL));

    float pw = text_width(0.48f, 0, mode_lbl) + 12.0f;
    u32 pill_bg = th->ink ? COLOR_ALPHA(th->ink->pill_fill, (int)(210 * a)) : COLOR_ALPHA(theme_toggle_on(th), (int)(190 * a));
    u32 pill_fg = fade(THEME_INK(th, pill_ink, TEXT_MAIN), a);
    gfx_round_rect(tx, 16.0f, pw, 16.0f, 8.0f, pill_bg);
    text_draw(tx + 6.0f, 27.5f, 0.48f, 0, pill_fg, ALIGN_LEFT, mode_lbl);

    /* Right side of Top Bar: Storage + Item count */
    char count_buf[32];
    snprintf(count_buf, sizeof(count_buf), L(S_F_ITEMS), s_view_n);
    text_draw(SCREEN_W - 16.0f, 27.0f, 0.52f, 0, main_c, ALIGN_RIGHT, count_buf);

    float cw = text_width(0.52f, 0, count_buf);
    const char *st_lbl = storage_filter_label();
    char st_short[40];
    text_ellipsis(st_short, sizeof(st_short), st_lbl, 0.48f, 110.0f);
    text_draw(SCREEN_W - 16.0f - cw - 10.0f, 27.0f, 0.48f, 0, dim_c, ALIGN_RIGHT, st_short);

    /* Sub-info line: Sort */
    char sort_line[64];
    snprintf(sort_line, sizeof(sort_line), "%s: %s", L(S_SORT), sort_label());
    text_draw(LIST_X + slide, 47.0f, 0.48f, 0, dim_c, ALIGN_LEFT, sort_line);
}

static void draw_placeholder_icon(const Theme *th, float x, float y, float w, float h, const SaveEntry *e, float a)
{
    u32 card_bg = th->ink ? COLOR_ALPHA(th->ink->panel_deep, (int)(235 * a)) : fade(th->card, a);
    u32 br = th->ink ? COLOR_ALPHA(th->ink->border, (int)(190 * a)) : RGBA(255, 255, 255, (int)(45 * a));
    gfx_round_rect(x, y, w, h, 7.0f, card_bg);
    gfx_ring(x, y, w, h, 7.0f, br);

    u32 fg = fade(THEME_INK(th, text_dim, TEXT_DIM), a);
    const char *tag = e->is_protected ? "ARK" : e->game_id[0] ? e->game_id : "SAVE";
    text_draw(x + w * 0.5f, y + h * 0.5f + 4.0f, 0.58f, 1, fg, ALIGN_CENTER, tag);
}

static void draw_save_icon(const Theme *th, float x, float y, float w, float h, int all_idx, float a)
{
    Texture *tex = icon_cache_peek(all_idx);
    if (!tex && S.icon_wait <= 0.0f)
        tex = icon_cache_get(all_idx);
    gfx_glow(x + 1, y + 2, w - 2, h - 2, 4, RGBA(0, 0, 0, (int)(110 * a)));
    if (tex)
        gfx_tex(tex, x, y, w, h, RGBA(255, 255, 255, (int)(255 * a)));
    else
        draw_placeholder_icon(th, x, y, w, h, &s_all[all_idx], a);
}

static void draw_hints_bar(const Theme *th, float a)
{
    struct { int btn; const char *label; } h[4];
    int n = 0;
    int confirm_btn = S.confirm_circle ? BTN_CIRCLE : BTN_CROSS;
    int back_btn = S.confirm_circle ? BTN_CROSS : BTN_CIRCLE;

    if (S.mode == MODE_DETAILS) {
        h[n++] = (typeof(h[0])){ BTN_TRIANGLE, L(S_F_DELETE) };
        h[n++] = (typeof(h[0])){ back_btn, L(S_BACK) };
    } else if (S.mode == MODE_CONFIRM_DEL || S.mode == MODE_PROTECTED_WARN) {
        h[n++] = (typeof(h[0])){ confirm_btn, L(S_SELECT) };
        h[n++] = (typeof(h[0])){ back_btn, L(S_BACK) };
    } else {
        if (s_view_n > 0) {
            h[n++] = (typeof(h[0])){ confirm_btn, L(S_SD_DETAILS) };
            h[n++] = (typeof(h[0])){ BTN_SQUARE, L(S_SORT) };
            h[n++] = (typeof(h[0])){ BTN_TRIANGLE, L(S_OPTIONS) };
        } else if (S.has_game_filter && S.filter_active) {
            h[n++] = (typeof(h[0])){ confirm_btn, L(S_SD_SHOW_ALL) };
        }
        h[n++] = (typeof(h[0])){ back_btn, L(S_BACK) };
    }

    float w = -12.0f;
    for (int i = 0; i < n; i++)
        w += 18.0f + text_width(0.54f, 0, h[i].label) + 12.0f;
    float x = SCREEN_W - 16.0f - w, y = 255.0f;
    u32 lbl = fade(THEME_INK(th, text, TEXT_MAIN), a);
    u32 dark_mark = fade(DARK, a);
    u32 light_plate = fade(LIGHT, 0.96f * a);

    for (int i = 0; i < n; i++) {
        theme_button(th, x + 6.5f, y, 6.5f, h[i].btn, 0, light_plate, dark_mark);
        x += 18.0f;
        text_draw(x, y + 4.5f, 0.54f, 0, lbl, ALIGN_LEFT, h[i].label);
        x += text_width(0.54f, 0, h[i].label) + 12.0f;
    }
}

static void draw_list_view(const Theme *th, float a, float slide)
{
    if (s_view_n == 0) {
        draw_card(th, LIST_X, LIST_Y, SCREEN_W - 2 * LIST_X, LIST_H, 10.0f, a);
        const char *msg = (S.has_game_filter && S.filter_active) ? L(S_SD_NO_GAME_DATA) : L(S_SD_NO_DATA);
        text_draw(SCREEN_W * 0.5f, 134.0f, 0.66f, 0, fade(THEME_INK(th, text, TEXT_MAIN), a), ALIGN_CENTER, msg);
        if (S.has_game_filter && S.filter_active) {
            const char *btn_lbl = L(S_SD_SHOW_ALL);
            float bw = text_width(0.58f, 0, btn_lbl) + 40.0f;
            float bx = (SCREEN_W - bw) * 0.5f, by = 158.0f;
            u32 pill_bg = fade(th->ink ? th->ink->pill_fill : LIGHT, a);
            u32 pill_fg = fade(THEME_INK(th, pill_ink, DARK), a);
            gfx_round_rect(bx, by, bw, 24.0f, 12.0f, pill_bg);
            theme_button(th, bx + 13.0f, by + 12.0f, 6.5f, S.confirm_circle ? BTN_CIRCLE : BTN_CROSS, 1,
                         fade(LIGHT, a), fade(DARK, a));
            text_draw(bx + 24.0f, by + 16.0f, 0.58f, 0, pill_fg, ALIGN_LEFT, btn_lbl);
        }
        return;
    }

    /* Left list card */
    draw_card(th, LIST_X, LIST_Y, LIST_W, LIST_H, 10.0f, a);
    float inner_top = LIST_Y + 5.0f;
    float inner_bottom = inner_top + LIST_ROWS * ROW_H;
    gfx_clip(LIST_X + 2.0f, inner_top - 2.0f, LIST_W - 4.0f, LIST_ROWS * ROW_H + 4.0f);

    float top_y = inner_top - S.scroll;
    for (int i = 0; i < s_view_n; i++) {
        float y = top_y + i * ROW_H;
        if (y < inner_top - ROW_H || y > inner_bottom)
            continue;
        float edge = clampf((y - (inner_top - ROW_H * 0.5f)) / (ROW_H * 0.55f), 0.0f, 1.0f) *
                     clampf((inner_bottom - y) / (ROW_H * 0.55f), 0.0f, 1.0f);
        float ra = a * edge;
        const SaveEntry *e = &s_all[s_view[i]];
        int sel = (i == S.sel && S.mode == MODE_LIST);

        if (sel) {
            if (th->ink) {
                theme_sel_box(th, LIST_X + 6.0f, y + 1.0f, LIST_W - 12.0f, ROW_H - 2.0f, 7.0f, fade(LIGHT, ra));
            } else {
                gfx_round_rect(LIST_X + 6.0f, y + 1.0f, LIST_W - 12.0f, ROW_H - 2.0f, 7.0f, RGBA(30, 44, 66, (int)(175 * ra)));
                gfx_ring(LIST_X + 6.0f, y + 1.0f, LIST_W - 12.0f, ROW_H - 2.0f, 7.0f, fade(RGBA(236, 240, 246, 245), ra));
            }
        } else if (i < s_view_n - 1 && !(i + 1 == S.sel && S.mode == MODE_LIST)) {
            u32 sep = th->ink ? COLOR_ALPHA(th->ink->border, (int)(150 * ra)) : RGBA(44, 56, 74, (int)(180 * ra));
            gfx_rect(LIST_X + 10.0f, y + ROW_H - 0.5f, LIST_W - 20.0f, 1.0f, sep);
        }

        u32 main_c = fade(sel ? THEME_INK(th, sel_ink, TEXT_MAIN) : THEME_INK(th, text, TEXT_MAIN), ra);
        u32 dim_c = fade(sel ? THEME_INK(th, sel_ink, TEXT_DIM) : THEME_INK(th, text_dim, TEXT_DIM), ra);

        float text_x = LIST_X + 12.0f + slide * 0.5f;
        float right_x = LIST_X + LIST_W - 12.0f;
        float badge_w = 0.0f;
        if (e->status != SAVE_OK) {
            const char *st = status_text(e->status);
            badge_w = text_width(0.40f, 1, st) + 8.0f;
            u32 sc = status_color(th, e->status);
            gfx_round_rect(right_x - badge_w, y + 5.0f, badge_w, 12.0f, 4.0f, COLOR_ALPHA(sc, (int)(48 * ra)));
            text_draw(right_x - badge_w * 0.5f, y + 14.0f, 0.40f, 1, fade(sc, ra), ALIGN_CENTER, st);
            badge_w += 6.0f;
        }

        char line1[96];
        text_ellipsis(line1, sizeof(line1), e->game_title, 0.54f, LIST_W - 24.0f - badge_w);
        text_draw(text_x, y + 14.5f, 0.54f, 0, main_c, ALIGN_LEFT, line1);

        char sz[24] = "...";
        if (e->measured)
            fmt_size(sz, sizeof(sz), e->size_bytes);
        float sz_w = text_width(0.45f, 0, sz);
        text_draw(right_x, y + 27.5f, 0.45f, 0, dim_c, ALIGN_RIGHT, sz);

        const char *sub = e->save_title[0] ? e->save_title : e->folder;
        char line2[96];
        text_ellipsis(line2, sizeof(line2), sub, 0.45f, LIST_W - 28.0f - sz_w);
        text_draw(text_x, y + 27.5f, 0.45f, 0, dim_c, ALIGN_LEFT, line2);
    }
    gfx_clip_reset();

    if (s_view_n > LIST_ROWS) {
        float bh = LIST_ROWS * ROW_H - 4.0f;
        float th_bar = clampf(bh * LIST_ROWS / s_view_n, 14.0f, bh);
        float frac = clampf((S.scroll / ROW_H) / (s_view_n - LIST_ROWS), 0.0f, 1.0f);
        float ty = inner_top + 2.0f + (bh - th_bar) * frac;
        u32 bar = th->ink ? COLOR_ALPHA(th->ink->shadow, 140) : RGBA(255, 255, 255, 70);
        gfx_round_rect(LIST_X + LIST_W - 5.0f, ty, 3.0f, th_bar, 1.5f, fade(bar, a));
    }

    /* Right preview card */
    draw_card(th, PREV_X, PREV_Y, PREV_W, PREV_H, 10.0f, a);
    int sel_all = s_view[S.sel];
    const SaveEntry *cur = &s_all[sel_all];

    float iw = 136.0f, ih = 76.0f;
    float ix = PREV_X + (PREV_W - iw) * 0.5f, iy = PREV_Y + 10.0f;
    draw_save_icon(th, ix, iy, iw, ih, sel_all, a);

    float px = PREV_X + 12.0f, pw = PREV_W - 24.0f;
    float py = iy + ih + 14.0f;
    u32 main_c = fade(THEME_INK(th, text, TEXT_MAIN), a);
    u32 dim_c = fade(THEME_INK(th, text_dim, TEXT_DIM), a);

    char tbuf[96];
    text_ellipsis(tbuf, sizeof(tbuf), cur->game_title, 0.56f, pw);
    text_draw(px, py, 0.56f, 1, main_c, ALIGN_LEFT, tbuf);
    py += 14.0f;

    text_ellipsis(tbuf, sizeof(tbuf), cur->save_title[0] ? cur->save_title : cur->folder, 0.48f, pw);
    text_draw(px, py, 0.48f, 0, dim_c, ALIGN_LEFT, tbuf);
    py += 14.0f;

    char id_folder[96];
    if (cur->game_id[0])
        snprintf(id_folder, sizeof(id_folder), "%s · %s", cur->game_id, cur->folder);
    else
        snprintf(id_folder, sizeof(id_folder), "%s", cur->folder);
    text_ellipsis(tbuf, sizeof(tbuf), id_folder, 0.44f, pw);
    text_draw(px, py, 0.44f, 0, dim_c, ALIGN_LEFT, tbuf);
    py += 13.0f;

    char dt_buf[32] = "-", sz_buf[24];
    if (cur->mtime)
        dt_text_stored(dt_buf, sizeof(dt_buf), cur->mtime);
    if (cur->measured)
        fmt_size(sz_buf, sizeof(sz_buf), cur->size_bytes);
    else
        snprintf(sz_buf, sizeof(sz_buf), "...");
    char meta_line[64];
    snprintf(meta_line, sizeof(meta_line), "%s · %s", sz_buf, dt_buf);
    text_ellipsis(tbuf, sizeof(tbuf), meta_line, 0.44f, pw);
    text_draw(px, py, 0.44f, 0, dim_c, ALIGN_LEFT, tbuf);

    if (cur->status != SAVE_OK) {
        const char *st = status_text(cur->status);
        u32 sc = status_color(th, cur->status);
        float sw = text_width(0.46f, 1, st) + 12.0f;
        gfx_round_rect(px, PREV_Y + PREV_H - 22.0f, sw, 14.0f, 5.0f, COLOR_ALPHA(sc, (int)(55 * a)));
        text_draw(px + sw * 0.5f, PREV_Y + PREV_H - 12.0f, 0.46f, 1, fade(sc, a), ALIGN_CENTER, st);
    } else {
        const char *loc = L(cur->storage == SAVE_DEV_EF0 ? S_F_INTERNAL : S_F_MS);
        text_draw(px, PREV_Y + PREV_H - 12.0f, 0.44f, 0, dim_c, ALIGN_LEFT, loc);
    }
}

static void draw_details_view(const Theme *th, float a, float slide)
{
    int idx = S.det_all_idx;
    if (idx < 0 || idx >= s_all_n)
        return;
    const SaveEntry *e = &s_all[idx];
    u32 main_c = fade(THEME_INK(th, text, TEXT_MAIN), a);
    u32 dim_c = fade(THEME_INK(th, text_dim, TEXT_DIM), a);

    text_draw(DET_LEFT_X + slide, 34.0f, 0.88f, 1, main_c, ALIGN_LEFT, L(S_SD_DETAILS));

    /* Left column: Icon, title, game ID, status */
    float iy = DET_RIGHT_Y, ih = 80.0f;
    draw_save_icon(th, DET_LEFT_X + slide, iy, DET_LEFT_W, ih, idx, a);

    char lines[3][96];
    int nl = text_wrap(e->game_title, 0.58f, DET_LEFT_W, lines, 3);
    float ty = iy + ih + 18.0f;
    for (int i = 0; i < nl; i++) {
        text_draw(DET_LEFT_X + slide, ty + i * 14.0f, 0.58f, 1, main_c, ALIGN_LEFT, lines[i]);
    }
    ty += nl * 14.0f + 4.0f;
    if (e->game_id[0]) {
        text_draw(DET_LEFT_X + slide, ty, 0.50f, 0, dim_c, ALIGN_LEFT, e->game_id);
        ty += 15.0f;
    }
    if (e->status != SAVE_OK) {
        const char *st = status_text(e->status);
        u32 sc = status_color(th, e->status);
        float sw = text_width(0.48f, 1, st) + 12.0f;
        gfx_round_rect(DET_LEFT_X + slide, ty, sw, 15.0f, 5.0f, COLOR_ALPHA(sc, (int)(55 * a)));
        text_draw(DET_LEFT_X + slide + sw * 0.5f, ty + 10.5f, 0.48f, 1, fade(sc, a), ALIGN_CENTER, st);
    }

    /* Right column: scrollable details card */
    draw_card(th, DET_RIGHT_X, DET_RIGHT_Y, DET_RIGHT_W, DET_RIGHT_H, 10.0f, a);
    float top_clip = DET_RIGHT_Y + 6.0f;
    float bot_clip = top_clip + DET_ROWS * DET_ROW_H;
    gfx_clip(DET_RIGHT_X + 2.0f, top_clip - 2.0f, DET_RIGHT_W - 4.0f, DET_ROWS * DET_ROW_H + 4.0f);

    float top_y = top_clip - S.det_scroll;
    for (int i = 0; i < S.det_n; i++) {
        float y = top_y + i * DET_ROW_H;
        if (y < top_clip - DET_ROW_H || y > bot_clip)
            continue;
        float edge = clampf((y - (top_clip - DET_ROW_H * 0.5f)) / (DET_ROW_H * 0.55f), 0.0f, 1.0f) *
                     clampf((bot_clip - y) / (DET_ROW_H * 0.55f), 0.0f, 1.0f);
        float ra = a * edge;
        const DetailRow *r = &S.det_rows[i];
        int sel = (i == S.det_sel && S.mode == MODE_DETAILS);

        if (sel) {
            if (th->ink) {
                theme_sel_box(th, DET_RIGHT_X + 6.0f, y, DET_RIGHT_W - 12.0f, DET_ROW_H, 6.0f, fade(LIGHT, ra));
            } else {
                gfx_round_rect(DET_RIGHT_X + 6.0f, y, DET_RIGHT_W - 12.0f, DET_ROW_H, 6.0f, RGBA(30, 44, 66, (int)(175 * ra)));
                gfx_ring(DET_RIGHT_X + 6.0f, y, DET_RIGHT_W - 12.0f, DET_ROW_H, 6.0f, fade(RGBA(236, 240, 246, 245), ra));
            }
        } else if (i < S.det_n - 1 && !(i + 1 == S.det_sel && S.mode == MODE_DETAILS)) {
            u32 sep = th->ink ? COLOR_ALPHA(th->ink->border, (int)(140 * ra)) : RGBA(44, 56, 74, (int)(170 * ra));
            gfx_rect(DET_RIGHT_X + 10.0f, y + DET_ROW_H - 0.5f, DET_RIGHT_W - 20.0f, 1.0f, sep);
        }

        u32 lc = fade(sel ? THEME_INK(th, sel_ink, TEXT_MAIN) : THEME_INK(th, text, TEXT_MAIN), ra);
        u32 vc = r->is_status ? fade(status_color(th, e->status), ra)
                              : fade(sel ? THEME_INK(th, sel_ink, TEXT_DIM) : THEME_INK(th, text_dim, TEXT_DIM), ra);

        float ls = r->label[0] ? text_fit(0.54f, 0.44f, 0, 96.0f, r->label) : 0.54f;
        if (r->label[0])
            text_draw(DET_RIGHT_X + 12.0f, y + 16.0f, ls, 0, lc, ALIGN_LEFT, r->label);

        float max_vw = r->label[0] ? (DET_RIGHT_W - 122.0f) : (DET_RIGHT_W - 28.0f);
        char shown[160];
        float vs = text_fit(0.52f, 0.42f, 0, max_vw, r->value);
        text_ellipsis(shown, sizeof(shown), r->value, vs, max_vw);
        text_draw(DET_RIGHT_X + DET_RIGHT_W - 12.0f, y + 16.0f, vs, 0, vc, ALIGN_RIGHT, shown);
    }
    gfx_clip_reset();
}

static void draw_options_menu(const Theme *th)
{
    float a = S.menu_anim;
    if (a <= 0.01f)
        return;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(110 * a)));
    float mw = 216.0f, row_h = 26.0f;
    float mh = 30.0f + S.menu_n * row_h + 8.0f;
    float x = SCREEN_W - 20.0f - mw, y = 238.0f - mh + (1.0f - a) * 10.0f;

    gfx_glow(x, y, mw, mh, 10, RGBA(0, 0, 0, (int)(150 * a)));
    if (th->ink) {
        gfx_round_rect(x, y, mw, mh, 9, fade(th->ink->border, a));
        gfx_round_rect(x + 1, y + 1, mw - 2, mh - 2, 8.2f, fade(th->ink->panel, a));
    } else {
        gfx_round_rect(x, y, mw, mh, 9, fade(PANEL, a));
    }

    const char *hdr = s_view_n > 0 ? s_all[s_view[S.sel]].folder : L(S_OPTIONS);
    char hdr_buf[64];
    text_ellipsis(hdr_buf, sizeof(hdr_buf), hdr, 0.50f, mw - 24.0f);
    text_draw(x + 12.0f, y + 19.0f, 0.50f, 0, fade(THEME_INK(th, text_dim, TEXT_DIM), a), ALIGN_LEFT, hdr_buf);

    for (int i = 0; i < S.menu_n; i++) {
        float ry = y + 26.0f + i * row_h;
        int act = S.menu_act[i];
        char lbl[80];
        if (act == MA_DETAILS)
            snprintf(lbl, sizeof(lbl), "%s", L(S_SD_DETAILS));
        else if (act == MA_DELETE)
            snprintf(lbl, sizeof(lbl), "%s", L(S_F_DELETE));
        else if (act == MA_SORT)
            snprintf(lbl, sizeof(lbl), "%s: %s", L(S_SORT), sort_label());
        else if (act == MA_TOGGLE_FILTER)
            snprintf(lbl, sizeof(lbl), "%s", S.filter_active ? L(S_SD_SHOW_ALL) : S.filter_title);
        else
            snprintf(lbl, sizeof(lbl), "%s", storage_filter_label());

        u32 col = (act == MA_DELETE) ? THEME_INK(th, err, DANGER) : THEME_INK(th, text, TEXT_MAIN);
        if (i == S.menu_sel) {
            theme_sel_box(th, x + 6.0f, ry, mw - 12.0f, row_h - 2.0f, 7.0f, fade(LIGHT, 0.95f * a));
            col = (act == MA_DELETE) ? THEME_INK(th, err, RGBA(200, 50, 40, 255)) : THEME_INK(th, sel_ink, DARK);
        }
        float fs = text_fit(0.58f, 0.44f, 0, mw - 28.0f, lbl);
        text_draw(x + 14.0f, ry + 17.0f, fs, 0, fade(col, a), ALIGN_LEFT, lbl);
    }
}

static void draw_confirm_modal(const Theme *th)
{
    if (S.mode != MODE_CONFIRM_DEL)
        return;
    int idx = S.target_all_idx;
    if (idx < 0 || idx >= s_all_n)
        return;
    const SaveEntry *e = &s_all[idx];

    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, 145));
    float w = 324.0f, h = 148.0f;
    float x = (SCREEN_W - w) * 0.5f, y = (SCREEN_H - h) * 0.5f;
    gfx_glow(x, y, w, h, 12, RGBA(0, 0, 0, 170));
    if (th->ink) {
        gfx_round_rect(x, y, w, h, 10, th->ink->border);
        gfx_round_rect(x + 1, y + 1, w - 2, h - 2, 9.2f, th->ink->panel);
    } else {
        gfx_round_rect(x, y, w, h, 10, PANEL);
    }

    text_draw(x + 16.0f, y + 26.0f, 0.68f, 1, THEME_INK(th, err, DANGER), ALIGN_LEFT, L(S_SD_CONFIRM_DEL));

    char buf[128];
    text_ellipsis(buf, sizeof(buf), e->game_title, 0.56f, w - 32.0f);
    text_draw(x + 16.0f, y + 48.0f, 0.56f, 0, THEME_INK(th, text, TEXT_MAIN), ALIGN_LEFT, buf);

    char sub[160], sz[24];
    if (e->measured)
        fmt_size(sz, sizeof(sz), e->size_bytes);
    else
        snprintf(sz, sizeof(sz), "%s", L(S_SD_CALCULATING));
    if (e->save_title[0])
        snprintf(sub, sizeof(sub), "%s · %s (%s)", e->folder, e->save_title, sz);
    else
        snprintf(sub, sizeof(sub), "%s (%s)", e->folder, sz);
    text_ellipsis(buf, sizeof(buf), sub, 0.48f, w - 32.0f);
    text_draw(x + 16.0f, y + 66.0f, 0.48f, 0, THEME_INK(th, text_dim, TEXT_DIM), ALIGN_LEFT, buf);

    const char *btns[2] = { L(S_F_DELETE), L(S_F_CANCEL) };
    float bw = 136.0f, bh = 28.0f, by = y + h - 42.0f;
    for (int i = 0; i < 2; i++) {
        float bx = x + 16.0f + i * (bw + 20.0f);
        int sel = (S.del_sel == i);
        u32 col = (i == 0) ? THEME_INK(th, err, DANGER) : THEME_INK(th, text, TEXT_MAIN);
        if (sel) {
            theme_sel_box(th, bx, by, bw, bh, 7.0f, LIGHT);
            col = (i == 0) ? THEME_INK(th, err, RGBA(195, 48, 38, 255)) : THEME_INK(th, sel_ink, DARK);
        } else {
            u32 idle_bg = th->ink ? th->ink->panel_alt : RGBA(48, 52, 64, 220);
            gfx_round_rect(bx, by, bw, bh, 7.0f, idle_bg);
        }
        text_draw(bx + bw * 0.5f, by + 18.5f, 0.58f, sel, col, ALIGN_CENTER, btns[i]);
    }
}

static void draw_protected_modal(const Theme *th)
{
    if (S.mode != MODE_PROTECTED_WARN)
        return;
    int idx = S.target_all_idx;
    const char *folder = (idx >= 0 && idx < s_all_n) ? s_all[idx].folder : "ARK_01234";

    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, 150));
    float w = 360.0f, h = 152.0f;
    float x = (SCREEN_W - w) * 0.5f, y = (SCREEN_H - h) * 0.5f;
    gfx_glow(x, y, w, h, 12, RGBA(0, 0, 0, 175));
    if (th->ink) {
        gfx_round_rect(x, y, w, h, 10, th->ink->border);
        gfx_round_rect(x + 1, y + 1, w - 2, h - 2, 9.2f, th->ink->panel);
    } else {
        gfx_round_rect(x, y, w, h, 10, PANEL);
    }

    char title[80];
    snprintf(title, sizeof(title), "%s · %s", L(S_SD_PROTECTED), folder);
    text_draw(x + 16.0f, y + 26.0f, 0.66f, 1, THEME_INK(th, warn, WARN_COL), ALIGN_LEFT, title);

    char lines[4][96];
    int nl = text_wrap(L(S_SD_PROTECTED_MSG), 0.52f, w - 32.0f, lines, 4);
    for (int i = 0; i < nl; i++)
        text_draw(x + 16.0f, y + 50.0f + i * 15.0f, 0.52f, 0, THEME_INK(th, text, TEXT_MAIN), ALIGN_LEFT, lines[i]);

    float bw = 110.0f, bh = 26.0f;
    float bx = x + (w - bw) * 0.5f, by = y + h - 38.0f;
    theme_sel_box(th, bx, by, bw, bh, 7.0f, LIGHT);
    text_draw(bx + bw * 0.5f, by + 17.5f, 0.56f, 1, THEME_INK(th, sel_ink, DARK), ALIGN_CENTER, L(S_BACK));
}

void savedata_draw(void)
{
    const Theme *th = theme_cur();
    static const float neutral[3] = { 120, 120, 120 };
    float a = S.open, slide = (1.0f - a) * 10.0f;

    gfx_begin(THEME_INK(th, panel_alt, RGBA(9, 13, 19, 255)));
    ui_draw_calm_backdrop(th, S.time, neutral, 0);

    if (S.mode == MODE_DETAILS || ((S.mode == MODE_CONFIRM_DEL || S.mode == MODE_PROTECTED_WARN) && S.prev_mode == MODE_DETAILS)) {
        draw_details_view(th, a, slide);
    } else {
        draw_top_header(th, a, slide);
        draw_list_view(th, a, slide);
    }

    draw_hints_bar(th, a);
    draw_options_menu(th);
    draw_confirm_modal(th);
    draw_protected_modal(th);

    if (S.toast_time > 0) {
        float ta = clampf(S.toast_time / 0.4f, 0.0f, 1.0f);
        float tw = text_width(0.6f, 0, S.toast) + 30.0f;
        gfx_round_rect((SCREEN_W - tw) * 0.5f, 128.0f, tw, 26.0f, 7.0f,
                       COLOR_ALPHA(THEME_INK(th, graphite, RGBA(44, 45, 52, 255)), (int)(245 * ta)));
        text_draw(SCREEN_W * 0.5f, 145.0f, 0.6f, 0,
                  COLOR_ALPHA(THEME_INK(th, text_inverse, RGBA(240, 240, 245, 255)), (int)(255 * ta)),
                  ALIGN_CENTER, S.toast);
    }

    osd_draw();
    if (a < 1.0f)
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(255 * (1.0f - a))));
    gfx_end();
}
