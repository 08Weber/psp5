/* psp5 – made by Mille Weber */
#include <pspkernel.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include "scan.h"
#include "sfo.h"
#include "iso.h"
#include "system.h"

#define MAX_SFO_SIZE (64 * 1024)
#define MAX_ISO_DEPTH 1 /* ISO-mappen + en nivå undermappar */

#define PBP_MAGIC 0x50425000   /* "\0PBP" */
#define CACHE_MAGIC 0x324C564E /* "NVL2" */

typedef struct {
    u32 magic;
    u32 version;
    u32 param_offset;
    u32 icon0_offset;
    u32 icon1_offset;
    u32 pic0_offset;
    u32 pic1_offset;
    u32 snd0_offset;
    u32 psp_offset;
    u32 psar_offset;
} PbpHeader;

typedef struct {
    u32 magic;
    u32 entry_size;
    u32 count;
} CacheHeader;

typedef struct {
    EntryList *list;
    EntryList cached;
    ScanProgress progress;
    int parsed;
} ScanState;

static Entry *add_entry(EntryList *list)
{
    if (list->count == list->capacity) {
        int cap = list->capacity ? list->capacity * 2 : 64;
        Entry *items = realloc(list->items, cap * sizeof(Entry));
        if (!items)
            return NULL;
        list->items = items;
        list->capacity = cap;
    }
    Entry *e = &list->items[list->count++];
    memset(e, 0, sizeof(*e));
    return e;
}

static u32 pack_time(const ScePspDateTime *t)
{
    return ((t->year - 1980) << 26) | (t->month << 22) | (t->day << 17) |
           (t->hour << 12) | (t->minute << 6) | t->second;
}

/* Finns filen oförändrad i cachen? Kopierar i så fall posten. sort_time läses alltid
 * på nytt: Homebrew Sorter ändrar mappens datum men inte filen. */
static int from_cache(ScanState *s, const char *path, u32 size, u32 time, u32 sort_time)
{
    for (int i = 0; i < s->cached.count; i++) {
        const Entry *c = &s->cached.items[i];
        if (c->file_size == size && c->file_time == time && strcmp(c->path, path) == 0) {
            Entry *e = add_entry(s->list);
            if (e) {
                *e = *c;
                e->sort_time = sort_time;
            }
            return 1;
        }
    }
    return 0;
}

static void report(ScanState *s, const char *name)
{
    s->parsed++;
    if (s->progress)
        s->progress(s->parsed, name);
}

static int has_ext(const char *name, const char *ext)
{
    const char *dot = strrchr(name, '.');
    return dot && strcasecmp(dot + 1, ext) == 0;
}

/* Tar bort ™ och ® – de ser konstiga ut i systemtypsnittet och skulle annars
 * tvinga fram det stora japanska typsnittet för vanliga västerländska titlar. */
static void strip_marks(char *s)
{
    static const char *marks[] = { "\xE2\x84\xA2", "\xC2\xAE" };
    for (int m = 0; m < 2; m++) {
        int len = strlen(marks[m]);
        char *p;
        while ((p = strstr(s, marks[m])))
            memmove(p, p + len, strlen(p + len) + 1);
    }
}

void entry_read_sfo(Entry *e, const u8 *sfo, u32 size)
{
    sfo_get_string(sfo, size, "TITLE", e->title, sizeof(e->title));
    strip_marks(e->title);
    sfo_get_string(sfo, size, "DISC_ID", e->game_id, sizeof(e->game_id));
}

static void add_iso(ScanState *s, const char *path, const char *name, u32 size, u32 time)
{
    if (from_cache(s, path, size, time, time))
        return;
    report(s, name);

    IsoReader r;
    if (iso_open(&r, path) < 0)
        return;

    /* utan PARAM.SFO är det inget PSP-spel (eller så är filen trasig) */
    u32 sfo_size;
    u8 *sfo = iso_load(&r, "PSP_GAME/PARAM.SFO", &sfo_size, MAX_SFO_SIZE);
    Entry *e = sfo ? add_entry(s->list) : NULL;
    if (!e) {
        free(sfo);
        iso_close(&r);
        return;
    }
    strncpy(e->path, path, sizeof(e->path) - 1);
    e->type = ENTRY_ISO;
    e->file_size = size;
    e->file_time = e->sort_time = time;
    strncpy(e->title, name, sizeof(e->title) - 1);
    char *dot = strrchr(e->title, '.');
    if (dot)
        *dot = 0;
    entry_read_sfo(e, sfo, sfo_size);
    free(sfo);

    IsoFile f;
    if (iso_find(&r, "PSP_GAME/ICON0.PNG", &f)) {
        e->icon_offset = f.offset;
        e->icon_size = f.size;
    }
    if (iso_find(&r, "PSP_GAME/PIC1.PNG", &f)) {
        e->pic_offset = f.offset;
        e->pic_size = f.size;
    }
    if (iso_find(&r, "PSP_GAME/ICON1.PMF", &f)) {
        e->vid_offset = f.offset;
        e->vid_size = f.size;
    }
    if (iso_find(&r, "PSP_GAME/SND0.AT3", &f)) {
        e->snd_offset = f.offset;
        e->snd_size = f.size;
    }
    iso_close(&r);
}

static void scan_iso_dir(ScanState *s, const char *dir, int depth)
{
    SceUID d = sceIoDopen(dir);
    if (d < 0)
        return;

    SceIoDirent ent;
    char path[256];
    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(d, &ent) > 0) {
        if (ent.d_name[0] != '.') {
            snprintf(path, sizeof(path), "%s/%s", dir, ent.d_name);
            if (FIO_S_ISDIR(ent.d_stat.st_mode)) {
                if (depth < MAX_ISO_DEPTH)
                    scan_iso_dir(s, path, depth + 1);
            } else if (has_ext(ent.d_name, "iso") || has_ext(ent.d_name, "cso") ||
                       has_ext(ent.d_name, "zso") || has_ext(ent.d_name, "dax")) { /* libcisoread och ARK läser DAX */
                add_iso(s, path, ent.d_name, (u32)ent.d_stat.st_size, pack_time(&ent.d_stat.sce_st_mtime));
            }
        }
        memset(&ent, 0, sizeof(ent)); /* d_private måste vara nollad */
    }
    sceIoDclose(d);
}

/* 0 = mappen har ingen EBOOT.PBP. folder_time = mappens ändringstid (XMB-ordningen). */
static int add_eboot(ScanState *s, const char *path, const char *folder, u32 folder_time)
{
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0)
        return 0;
    u32 size = (u32)st.st_size, time = pack_time(&st.sce_st_mtime);
    if (from_cache(s, path, size, time, folder_time))
        return 1;
    report(s, folder);

    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return 1;

    PbpHeader h;
    if (sceIoRead(fd, &h, sizeof(h)) != sizeof(h) || h.magic != PBP_MAGIC) {
        sceIoClose(fd); /* t.ex. ren ELF (1.50) – hoppas över */
        return 1;
    }

    Entry *e = add_entry(s->list);
    if (!e) {
        sceIoClose(fd);
        return 1;
    }
    strncpy(e->path, path, sizeof(e->path) - 1);
    strncpy(e->title, folder, sizeof(e->title) - 1);
    e->type = ENTRY_HOMEBREW;
    e->file_size = size;
    e->file_time = time;
    e->sort_time = folder_time;

    u32 sfo_size = h.icon0_offset - h.param_offset;
    if (sfo_size > 0 && sfo_size <= MAX_SFO_SIZE) {
        u8 *sfo = malloc(sfo_size);
        if (sfo) {
            sceIoLseek32(fd, h.param_offset, PSP_SEEK_SET);
            if (sceIoRead(fd, sfo, sfo_size) == (int)sfo_size) {
                char cat[8] = "";
                entry_read_sfo(e, sfo, sfo_size);
                sfo_get_string(sfo, sfo_size, "CATEGORY", cat, sizeof(cat));
                if (strcmp(cat, "EG") == 0)
                    e->type = ENTRY_PSN;
                else if (strcmp(cat, "ME") == 0)
                    e->type = ENTRY_POPS;
            }
            free(sfo);
        }
    }

    e->icon_size = h.icon1_offset - h.icon0_offset;
    e->icon_offset = e->icon_size ? h.icon0_offset : 0;
    e->pic_size = h.snd0_offset - h.pic1_offset;
    e->pic_offset = e->pic_size ? h.pic1_offset : 0;
    e->vid_size = h.pic0_offset - h.icon1_offset;
    e->vid_offset = e->vid_size ? h.icon1_offset : 0;
    e->snd_size = h.psp_offset - h.snd0_offset;
    e->snd_offset = e->snd_size ? h.snd0_offset : 0;
    sceIoClose(fd);
    return 1;
}

/* depth 0 = PSP/GAME. En mapp utan EBOOT.PBP är en kategorimapp (t.ex. GAME/PS1/<spel>,
 * GAME/Emulators/TempGBA) – som arkMenu letar vi då en nivå ner i den. */
static void scan_game_dir(ScanState *s, const char *dir, const char *self_path, int depth)
{
    SceUID d = sceIoDopen(dir);
    if (d < 0)
        return;

    SceIoDirent ent;
    char path[256];
    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(d, &ent) > 0) {
        if (ent.d_name[0] != '.' && FIO_S_ISDIR(ent.d_stat.st_mode)) {
            snprintf(path, sizeof(path), "%s/%s/EBOOT.PBP", dir, ent.d_name);
            /* psp5 själv (som app) ska inte synas i listan */
            if (strcasecmp(ent.d_name, "PSP5") != 0 && (!self_path || strcasecmp(path, self_path) != 0) &&
                !add_eboot(s, path, ent.d_name, pack_time(&ent.d_stat.sce_st_mtime)) && depth == 0) {
                snprintf(path, sizeof(path), "%s/%s", dir, ent.d_name);
                scan_game_dir(s, path, self_path, 1);
            }
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(d);
}

static void load_cache(EntryList *cached, const char *file)
{
    SceUID fd = sceIoOpen(file, PSP_O_RDONLY, 0);
    if (fd < 0)
        return;
    CacheHeader h;
    if (sceIoRead(fd, &h, sizeof(h)) == sizeof(h) && h.magic == CACHE_MAGIC &&
        h.entry_size == sizeof(Entry) && h.count < 10000) {
        cached->items = malloc(h.count * sizeof(Entry));
        if (cached->items && sceIoRead(fd, cached->items, h.count * sizeof(Entry)) == (int)(h.count * sizeof(Entry)))
            cached->count = cached->capacity = h.count;
    }
    sceIoClose(fd);
}

static void save_cache(const EntryList *list, const char *file)
{
    SceUID fd = sceIoOpen(file, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0)
        return;
    CacheHeader h = { CACHE_MAGIC, sizeof(Entry), list->count };
    sceIoWrite(fd, &h, sizeof(h));
    sceIoWrite(fd, list->items, list->count * sizeof(Entry));
    sceIoClose(fd);
}

/* Skiftlägesokänslig jämförelse med teckenlösa byte, så att UTF-8-titlar
 * (t.ex. japanska) sorteras efter latinska i stället för före. */
static int compare_title(const void *a, const void *b)
{
    const u8 *x = (const u8 *)((const Entry *)a)->title;
    const u8 *y = (const u8 *)((const Entry *)b)->title;
    for (;; x++, y++) {
        int cx = *x >= 'A' && *x <= 'Z' ? *x + 32 : *x;
        int cy = *y >= 'A' && *y <= 'Z' ? *y + 32 : *y;
        if (cx != cy || !cx)
            return cx - cy;
    }
}

void scan_all(EntryList *list, const char *self_path, const char *cache_file, ScanProgress progress)
{
    ScanState s = { list, { 0 }, progress, 0 };
    if (cache_file)
        load_cache(&s.cached, cache_file);

    /* minneskortet och PSP Go:s internminne – ef0: finns bara på PSP Go. Startades psp5 från
     * internminnet pekar ARK om ms0: dit, men bara under psp5:s session: spelen måste få
     * sökvägen ef0:, annars startas de som från minneskortet och hittas inte (issue #6). */
    int ms0 = !sys_ms_is_ef();
    if (ms0)
        scan_iso_dir(&s, "ms0:/ISO", 0);
    scan_iso_dir(&s, "ef0:/ISO", 0);
    if (ms0)
        scan_game_dir(&s, "ms0:/PSP/GAME", self_path, 0);
    scan_game_dir(&s, "ef0:/PSP/GAME", self_path, 0);
    scan_free(&s.cached);

    if (list->count > 1)
        qsort(list->items, list->count, sizeof(Entry), compare_title);
    if (cache_file && s.parsed > 0)
        save_cache(list, cache_file);
}

void scan_free(EntryList *list)
{
    free(list->items);
    memset(list, 0, sizeof(*list));
}

int scan_insert_front(EntryList *list, const Entry *e)
{
    if (!add_entry(list))
        return 0;
    memmove(&list->items[1], &list->items[0], (list->count - 1) * sizeof(Entry));
    list->items[0] = *e;
    return 1;
}

void scan_remove_front(EntryList *list)
{
    if (!list->count)
        return;
    list->count--;
    memmove(&list->items[0], &list->items[1], list->count * sizeof(Entry));
}

void entry_cover_key(const Entry *e, char *out, int out_size)
{
    if ((e->type == ENTRY_ISO || e->type == ENTRY_UMD) && e->game_id[0]) {
        snprintf(out, out_size, "%s", e->game_id);
        return;
    }
    /* mappnamnet: ms0:/PSP/GAME/<mapp>/EBOOT.PBP – eller filnamnet utan ändelse */
    const char *end = strrchr(e->path, '/');
    const char *start = end;
    if (e->type != ENTRY_ISO && end) {
        while (start > e->path && start[-1] != '/')
            start--;
    } else if (end) {
        start = end + 1;
        end = strrchr(start, '.');
        if (!end)
            end = start + strlen(start);
    }
    if (!start || !end || end <= start) {
        snprintf(out, out_size, "unknown");
        return;
    }
    snprintf(out, out_size, "%.*s", (int)(end - start), start);
}
