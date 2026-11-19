/* PSP5 REMAKE – Game / App Information (see info.h). Adapted from psp5-authentic info.c and the
 * folder counting of its gameops.c. Nothing is stored in the game list for it. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "info.h"
#include "datetime.h"
#include "iso.h"
#include "lang.h"
#include "lastplayed.h"
#include "osd.h"
#include "sfo.h"
#include "sound.h"
#include "system.h"
#include "theme.h"
#include "umd.h"

#define TITLE_Y     34.0f
#define LEFT_X      24.0f
#define LEFT_W      136.0f
#define RIGHT_X     172.0f
#define RIGHT_W     284.0f
#define ROW_H       26.0f
#define LIST_TOP    58.0f
#define LIST_ROWS   7
#define LIST_BOTTOM (LIST_TOP + LIST_ROWS * ROW_H)

#define MAX_ROWS    40
#define MAX_SFO     (64 * 1024)
#define PBP_MAGIC   0x50425000
#define CSO_MAGIC   0x4F534943 /* "CISO" */
#define ZSO_MAGIC   0x4F53495A /* "ZISO" */
#define DAX_MAGIC   0x00584144 /* "DAX\0" */
#define MAX_PATH_LEN 512
#define MAX_DEPTH   12

#define TEXT_MAIN RGBA(238, 238, 242, 255)
#define TEXT_DIM  RGBA(160, 162, 172, 255)

typedef struct {
    char label[48];
    char value[160];
    char shown[176];
    float ls, vs;
    u8 header, left_cut;
} Row;

static Row s_rows[MAX_ROWS];
static int s_n;
static struct {
    float open, time, scroll;
    int sel, top, index, measure_row, confirm_circle;
    int repeat_dir;
    float repeat_timer;
    Entry e;
} S;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static u32 fade(u32 c, float a) { return COLOR_ALPHA(c, (int)(((c >> 24) & 0xFF) * clampf(a, 0, 1))); }
static float approach(float cur, float target, float speed, float dt) { return cur + (target - cur) * (1.0f - expf(-speed * dt)); }

/* ---------------------------------------------------------------------- */
/* folder size, counted in a thread of its own (the interface keeps moving) */

typedef struct {
    char name[256];
    u64 size;
    u8 dir;
} DirItem;

enum { M_IDLE, M_RUN, M_DONE };
static struct {
    SceUID thread;
    volatile int state, cancel;
    unsigned long long bytes;
    unsigned files;
    char path[MAX_PATH_LEN];
} M = { .thread = -1 };

static int read_dir(const char *path, DirItem **out)
{
    *out = NULL;
    SceUID d = sceIoDopen(path);
    if (d < 0)
        return -1;
    DirItem *list = NULL;
    int n = 0, cap = 0;
    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));
    while (!M.cancel && sceIoDread(d, &ent) > 0) {
        if (strcmp(ent.d_name, ".") && strcmp(ent.d_name, "..")) {
            if (n == cap) {
                cap = cap ? cap * 2 : 32;
                DirItem *grown = realloc(list, cap * sizeof(*list));
                if (!grown)
                    break;
                list = grown;
            }
            DirItem *it = &list[n++];
            snprintf(it->name, sizeof(it->name), "%s", ent.d_name);
            it->dir = FIO_S_ISDIR(ent.d_stat.st_mode) != 0;
            it->size = it->dir ? 0 : (u64)ent.d_stat.st_size;
        }
        memset(&ent, 0, sizeof(ent)); /* d_private must be zero */
    }
    sceIoDclose(d);
    *out = list;
    return n;
}

static void measure_walk(const char *path, int depth)
{
    if (depth > MAX_DEPTH || M.cancel)
        return;
    DirItem *list;
    int n = read_dir(path, &list);
    for (int i = 0; i < n && !M.cancel; i++) {
        if (list[i].dir) {
            char child[MAX_PATH_LEN];
            snprintf(child, sizeof(child), "%s/%s", path, list[i].name);
            measure_walk(child, depth + 1);
        } else {
            M.bytes += list[i].size;
            M.files++;
        }
    }
    free(list);
}

static int measure_thread(SceSize args, void *argp)
{
    measure_walk(M.path, 0);
    M.state = M_DONE;
    return 0;
}

static void measure_stop(void)
{
    if (M.thread >= 0) {
        M.cancel = 1;
        sceKernelWaitThreadEnd(M.thread, NULL);
        sceKernelDeleteThread(M.thread);
        M.thread = -1;
    }
    M.state = M_IDLE;
}

static void measure_start(const char *folder)
{
    measure_stop();
    M.cancel = 0;
    M.bytes = 0;
    M.files = 0;
    snprintf(M.path, sizeof(M.path), "%s", folder);
    M.state = M_RUN;
    M.thread = sceKernelCreateThread("psp5_measure", measure_thread, 0x30, 0x8000, PSP_THREAD_ATTR_USER, NULL);
    if (M.thread >= 0 && sceKernelStartThread(M.thread, 0, NULL) < 0) {
        sceKernelDeleteThread(M.thread);
        M.thread = -1;
    }
    if (M.thread < 0) { /* no thread: the row says so instead of freezing the screen */
        M.state = M_IDLE;
    }
}

static int measure_poll(unsigned long long *bytes, unsigned *files)
{
    if (M.state != M_DONE)
        return 0;
    if (M.thread >= 0) {
        sceKernelWaitThreadEnd(M.thread, NULL);
        sceKernelDeleteThread(M.thread);
        M.thread = -1;
    }
    *bytes = M.bytes;
    *files = M.files;
    return 1;
}

/* ---------------------------------------------------------------------- */

static void fmt_size(char *out, int size, unsigned long long b)
{
    if (b >= 1024ull * 1024 * 1024)
        snprintf(out, size, "%.1f GB", (float)b / (1024.0f * 1024.0f * 1024.0f));
    else if (b >= 1024ull * 1024)
        snprintf(out, size, "%.1f MB", (float)b / (1024.0f * 1024.0f));
    else if (b >= 1024)
        snprintf(out, size, "%u KB", (unsigned)(b / 1024));
    else
        snprintf(out, size, "%u B", (unsigned)b);
}

static Row *add_row(const char *label, const char *value)
{
    if (s_n >= MAX_ROWS)
        return NULL;
    Row *r = &s_rows[s_n++];
    memset(r, 0, sizeof(*r));
    snprintf(r->label, sizeof(r->label), "%s", label);
    snprintf(r->value, sizeof(r->value), "%s", value);
    return r;
}

static void add_header(const char *text)
{
    Row *r = add_row(text, "");
    if (r)
        r->header = 1;
}

static u32 le32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }

static int read_at(const char *path, u32 offset, void *buf, u32 n)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return -1;
    sceIoLseek32(fd, offset, PSP_SEEK_SET);
    int got = sceIoRead(fd, buf, n);
    sceIoClose(fd);
    return got;
}

/* free space of the card the path is on (the same devctl the file manager uses); 0 when unknown */
static int dev_space(const char *path, u64 *free_b)
{
    char dev[8];
    snprintf(dev, sizeof(dev), "%.4s", path);
    struct { u32 max_clusters, free_clusters, max_sectors, sector_size, sectors_per_cluster; } info;
    void *p = &info;
    memset(&info, 0, sizeof(info));
    if (sceIoDevctl(dev, 0x02425818, &p, sizeof(p), NULL, 0) < 0)
        return 0;
    *free_b = (u64)info.free_clusters * info.sector_size * info.sectors_per_cluster;
    return 1;
}

static void location_rows(const Entry *e)
{
    int internal = !strncasecmp(e->path, "ef0:", 4) || (!strncasecmp(e->path, "ms0:", 4) && sys_ms_is_ef());
    const char *name = L(internal ? S_F_INTERNAL : S_F_MS);
    u64 free_b;
    char value[96];
    if (dev_space(e->path, &free_b)) {
        char num[24], free_text[48];
        fmt_size(num, sizeof(num), free_b);
        snprintf(free_text, sizeof(free_text), L(S_F_FREE), num);
        snprintf(value, sizeof(value), "%s, %s", name, free_text);
    } else {
        snprintf(value, sizeof(value), "%s", name);
    }
    add_row(L(S_I_LOCATION), value);

    const char *slash = strrchr(e->path, '/');
    if (slash) {
        char folder[256];
        snprintf(folder, sizeof(folder), "%.*s", (int)(slash - e->path), e->path);
        Row *r = add_row(L(S_I_FOLDER), folder);
        if (r)
            r->left_cut = 1;
        add_row(L(S_I_FILE), slash + 1);
    }
}

static void time_row(const char *label, u32 t)
{
    if (!t)
        return;
    char text[40];
    dt_text_stored(text, sizeof(text), t);
    add_row(label, text);
}

static const char *const SFO_KEYS[] = { "DISC_ID", "DISC_VERSION", "APP_VER", "PSP_SYSTEM_VER" };

static void sfo_rows(const u8 *sfo, u32 size)
{
    int n = sfo_count(sfo, size);
    int header_done = 0;
    for (unsigned k = 0; k < sizeof(SFO_KEYS) / sizeof(SFO_KEYS[0]); k++) {
        for (int i = 0; i < n; i++) {
            char key[48], val[160];
            if (!sfo_entry(sfo, size, i, key, sizeof(key), val, sizeof(val)) || strcmp(key, SFO_KEYS[k]))
                continue;
            if (!header_done) {
                add_header("PARAM.SFO");
                header_done = 1;
            }
            add_row(key, val);
            break;
        }
    }
}

static void iso_rows(const Entry *e)
{
    u8 head[24];
    memset(head, 0, sizeof(head));
    read_at(e->path, 0, head, sizeof(head));
    u32 magic = le32(head);
    int packed = magic == CSO_MAGIC || magic == ZSO_MAGIC || magic == DAX_MAGIC;
    char buf[96], num[24];
    snprintf(buf, sizeof(buf), L(S_I_IMAGE), magic == CSO_MAGIC ? "CSO" : magic == ZSO_MAGIC ? "ZSO" : magic == DAX_MAGIC ? "DAX" : "ISO");
    add_row(L(S_I_TYPE), buf);
    location_rows(e);

    fmt_size(num, sizeof(num), e->file_size);
    add_row(L(S_I_SIZE), num);
    if (packed) { /* the unpacked size: 64 bits at 8 in a CSO/ZSO, 32 bits at 4 in a DAX */
        u64 total = magic == DAX_MAGIC ? le32(head + 4) : (u64)le32(head + 8) | ((u64)le32(head + 12) << 32);
        if (total) {
            char pct[24];
            fmt_size(num, sizeof(num), total);
            add_row(L(S_I_UNCOMPRESSED), num);
            snprintf(pct, sizeof(pct), "%u %%", (unsigned)((u64)e->file_size * 100 / total));
            add_row(L(S_I_COMPRESSION), pct);
        }
    }
    time_row(L(S_I_MODIFIED), e->file_time);
    time_row(L(S_I_LAST_PLAYED), lastplayed_time(e));

    IsoReader r;
    if (iso_open(&r, e->path) < 0)
        return;
    static u8 pvd[2048];
    if (iso_read(&r, pvd, sizeof(pvd), 16 * 2048) == (int)sizeof(pvd) && !memcmp(pvd + 1, "CD001", 5)) {
        char vol[40];
        snprintf(vol, sizeof(vol), "%.32s", (const char *)pvd + 40);
        for (int i = strlen(vol); i > 0 && vol[i - 1] == ' '; i--)
            vol[i - 1] = 0;
        if (vol[0])
            add_row(L(S_I_VOLUME), vol);
    }
    u32 sfo_size;
    u8 *sfo = iso_load(&r, "PSP_GAME/PARAM.SFO", &sfo_size, MAX_SFO);
    if (sfo) {
        sfo_rows(sfo, sfo_size);
        free(sfo);
    }
    iso_close(&r);
}

static void pbp_rows(const Entry *e)
{
    add_row(L(S_I_TYPE), L(e->type == ENTRY_PSN ? S_I_PSN : e->type == ENTRY_POPS ? S_I_POPS : S_I_HOMEBREW));
    location_rows(e);

    SceIoStat st;
    u32 total = e->file_size;
    if (sceIoGetstat(e->path, &st) >= 0)
        total = (u32)st.st_size;
    char num[24];
    fmt_size(num, sizeof(num), total);
    add_row(L(S_I_SIZE), num);
    if (add_row(L(S_I_CONTENTS), "..."))
        S.measure_row = s_n - 1;
    time_row(L(S_I_MODIFIED), e->file_time);
    time_row(L(S_I_LAST_PLAYED), lastplayed_time(e));

    u32 h[10];
    memset(h, 0, sizeof(h));
    if (read_at(e->path, 0, h, sizeof(h)) != (int)sizeof(h) || h[0] != PBP_MAGIC)
        return;
    u32 sfo_size = h[3] - h[2];
    if (h[3] > h[2] && sfo_size <= MAX_SFO && h[3] <= total) {
        u8 *sfo = malloc(sfo_size);
        if (sfo) {
            if (read_at(e->path, h[2], sfo, sfo_size) == (int)sfo_size)
                sfo_rows(sfo, sfo_size);
            free(sfo);
        }
    }
}

static void umd_rows(const Entry *e)
{
    add_row(L(S_I_TYPE), L(S_I_UMD));
    add_row(L(S_I_LOCATION), L(S_I_UMD_DRIVE));
    time_row(L(S_I_LAST_PLAYED), lastplayed_time(e));
    SceIoStat st;
    if (sceIoGetstat(UMD_ROOT "PSP_GAME/PARAM.SFO", &st) >= 0 && st.st_size > 0 && st.st_size <= MAX_SFO) {
        u8 *sfo = malloc(st.st_size);
        if (sfo) {
            if (read_at(UMD_ROOT "PSP_GAME/PARAM.SFO", 0, sfo, st.st_size) == (int)st.st_size)
                sfo_rows(sfo, st.st_size);
            free(sfo);
        }
    }
}

static void layout_row(Row *r)
{
    r->ls = text_fit(0.62f, 0.48f, 0, RIGHT_W * 0.5f, r->label);
    float avail = RIGHT_W - 16 - text_width(r->ls, 0, r->label) - 14;
    if (avail < 40)
        avail = 40;
    r->vs = text_fit(0.58f, 0.46f, 0, avail, r->value);
    snprintf(r->shown, sizeof(r->shown), "%s", r->value);
    if (text_width(r->vs, 0, r->value) <= avail)
        return;
    const char *p = r->value;
    if (r->left_cut) {
        while (text_width(r->vs, 0, p) > avail && strchr(p + 1, '/'))
            p = strchr(p + 1, '/');
        if (p != r->value)
            snprintf(r->shown, sizeof(r->shown), "...%s", p);
    }
    if (text_width(r->vs, 0, r->shown) > avail)
        text_ellipsis(r->shown, sizeof(r->shown), r->shown, r->vs, avail);
}

void info_open(const Entry *e, int list_index, int confirm_circle)
{
    measure_stop();
    memset(&S, 0, sizeof(S));
    s_n = 0;
    S.e = *e;
    S.index = list_index;
    S.measure_row = -1;
    S.confirm_circle = confirm_circle;

    if (e->type == ENTRY_ISO)
        iso_rows(e);
    else if (e->type == ENTRY_UMD)
        umd_rows(e);
    else
        pbp_rows(e);

    int cjk = 0;
    for (int i = 0; i < s_n; i++)
        for (const char *p = s_rows[i].value; *p; p++)
            cjk |= (u8)*p >= 0x80;
    for (const char *p = e->title; *p; p++)
        cjk |= (u8)*p >= 0x80;
    if (cjk)
        font_ensure_cjk();
    for (int i = 0; i < s_n; i++)
        layout_row(&s_rows[i]);
    while (S.sel < s_n && s_rows[S.sel].header)
        S.sel++;

    if (S.measure_row >= 0) {
        char folder[256];
        const char *slash = strrchr(e->path, '/');
        snprintf(folder, sizeof(folder), "%.*s", slash ? (int)(slash - e->path) : 0, e->path);
        measure_start(folder);
        if (M.thread < 0) { /* could not count: say so (never a made-up number) */
            snprintf(s_rows[S.measure_row].value, sizeof(s_rows[0].value), "-");
            layout_row(&s_rows[S.measure_row]);
            S.measure_row = -1;
        }
    }
}

void info_close(void)
{
    measure_stop();
}

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

int info_update(const UiInput *in, float dt)
{
    S.open = clampf(S.open + dt * 5.0f, 0, 1);
    S.time += dt;

    if (S.measure_row >= 0) {
        unsigned long long bytes;
        unsigned files;
        if (measure_poll(&bytes, &files)) {
            char num[24], text[160];
            Row *r = &s_rows[S.measure_row];
            fmt_size(num, sizeof(num), bytes);
            snprintf(text, sizeof(text), L(S_I_SIZE_FILES), num, (int)files);
            snprintf(r->value, sizeof(r->value), "%s", text);
            layout_row(r);
            S.measure_row = -1;
        }
    }

    int move = vertical(in, dt);
    if (move) {
        int next = S.sel + move;
        while (next >= 0 && next < s_n && s_rows[next].header)
            next += move;
        if (next >= 0 && next < s_n && next != S.sel) {
            S.sel = next;
            sound_play(SND_MOVE);
        }
    }
    u32 back = S.confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;
    if (S.open >= 1.0f && (in->pressed & back)) {
        sound_play(SND_DETAIL_OUT);
        return 1;
    }

    if (S.sel < S.top) {
        S.top = S.sel;
        while (S.top > 0 && s_rows[S.top - 1].header)
            S.top--;
    }
    if (S.sel > S.top + LIST_ROWS - 1)
        S.top = S.sel - (LIST_ROWS - 1);
    S.scroll = approach(S.scroll, S.top * ROW_H, 14.0f, dt);
    return 0;
}

static void draw_left(const Theme *th, float a, float slide)
{
    float x = LEFT_X + slide, y = LIST_TOP, w = LEFT_W, h = floorf(w * 80 / 144 + 0.5f);
    IconSlot *slot = loader_icon(S.index);
    gfx_glow(x + 1, y + 3, w - 2, h - 2, 5, RGBA(0, 0, 0, (int)(130 * a)));
    if (slot && slot->state == SLOT_READY && slot->tex)
        gfx_tex(slot->tex, x, y, w, h, RGBA(255, 255, 255, (int)(255 * a)));
    else
        gfx_round_rect(x, y, w, h, w / 12, fade(th->card, a));

    char lines[3][96];
    int n = text_wrap(S.e.title, 0.6f, w, lines, 3);
    float ty = y + h + 24;
    u32 main_c = THEME_INK(th, text, TEXT_MAIN), dim_c = THEME_INK(th, text_dim, TEXT_DIM);
    for (int i = 0; i < n; i++)
        text_draw(x, ty + i * 15, 0.6f, 0, fade(main_c, a), ALIGN_LEFT, lines[i]);
    if (S.e.type != ENTRY_HOMEBREW && S.e.game_id[0])
        text_draw(x, ty + n * 15 + 6, 0.56f, 0, fade(dim_c, a), ALIGN_LEFT, S.e.game_id);
}

static void draw_row(const Theme *th, const Row *r, float y, int selected, int last, int next_selected, float a)
{
    u32 main_c = THEME_INK(th, text, TEXT_MAIN), dim_c = THEME_INK(th, text_dim, TEXT_DIM);
    if (r->header) {
        text_draw(RIGHT_X + 8, y + 17, 0.52f, 0, fade(dim_c, a * 0.8f), ALIGN_LEFT, r->label);
        return;
    }
    if (selected) {
        gfx_rect(RIGHT_X, y, RIGHT_W, ROW_H, RGBA(255, 255, 255, (int)(16 * a)));
        gfx_frame(RIGHT_X, y, RIGHT_W, ROW_H, 1, fade(RGBA(232, 232, 236, 230), a));
    } else if (!last && !next_selected) {
        gfx_rect(RIGHT_X + 4, y + ROW_H - 1, RIGHT_W - 8, 1, RGBA(255, 255, 255, (int)(26 * a)));
    }
    text_draw(RIGHT_X + 8, y + 17, r->ls, 0, fade(main_c, a), ALIGN_LEFT, r->label);
    text_draw(RIGHT_X + RIGHT_W - 8, y + 17, r->vs, 0, fade(dim_c, a), ALIGN_RIGHT, r->shown);
}

void info_draw(void)
{
    const Theme *th = theme_cur();
    static const float neutral[3] = { 120, 120, 120 };
    float a = S.open, slide = (1 - a) * 10;
    gfx_begin(THEME_INK(th, panel_alt, RGBA(20, 20, 24, 255)));
    theme_backdrop(th, S.time, neutral, 1, 1);
    text_draw(LEFT_X + 4 + slide, TITLE_Y, 0.95f, 0, fade(THEME_INK(th, text, TEXT_MAIN), a), ALIGN_LEFT, L(S_INFO));
    draw_left(th, a, slide);

    gfx_clip(0, LIST_TOP - 2, SCREEN_W, LIST_ROWS * ROW_H + 4);
    float top = LIST_TOP - S.scroll;
    for (int i = 0; i < s_n; i++) {
        float y = top + i * ROW_H;
        if (y < LIST_TOP - ROW_H || y > LIST_BOTTOM)
            continue;
        float edge = clampf((y - (LIST_TOP - ROW_H * 0.6f)) / (ROW_H * 0.6f), 0, 1) *
                     clampf((LIST_BOTTOM - y) / (ROW_H * 0.6f), 0, 1);
        draw_row(th, &s_rows[i], y, i == S.sel, i == s_n - 1, i + 1 == S.sel, a * edge);
    }
    gfx_clip_reset();

    /* the back hint, like the other screens */
    {
        const char *back = L(S_BACK);
        float w = 13.0f + 5.0f + text_width(0.56f, 0, back);
        float x = SCREEN_W - 22.0f - w, y = 252.0f;
        theme_button(th, x + 6.5f, y, 6.5f, S.confirm_circle ? BTN_CROSS : BTN_CIRCLE, 0,
                     fade(RGBA(236, 236, 240, 255), a), fade(RGBA(18, 22, 30, 255), a));
        text_draw(x + 18.0f, y + 4.5f, 0.56f, 0, fade(THEME_INK(th, text, TEXT_MAIN), a), ALIGN_LEFT, back);
    }
    osd_draw();
    if (a < 1)
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(255 * (1 - a))));
    gfx_end();
}
