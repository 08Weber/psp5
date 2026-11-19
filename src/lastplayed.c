/* PSP5 REMAKE – last played times (see lastplayed.h). Ported from psp5-authentic lastplayed.c,
 * reduced to what psp5 itself can know (no helper plugin), packed with datetime.c. */
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lastplayed.h"
#include "datetime.h"

#define KEY_LEN   80
#define MAX_GAMES 4096
#define MAX_FILE  (MAX_GAMES * (KEY_LEN + 16))

typedef struct {
    char key[KEY_LEN];
    u32 time;
} Game;

static Game *s_games;
static int s_n, s_cap;
static char s_dir[16];

static void file_path(char *out, int size)
{
    snprintf(out, size, "%s/lastplayed.txt", s_dir);
}

static Game *find(const char *key)
{
    for (int i = 0; i < s_n; i++)
        if (!strcmp(s_games[i].key, key))
            return &s_games[i];
    return NULL;
}

static int save(void)
{
    char path[48];
    file_path(path, sizeof(path));
    char *text = malloc((size_t)s_n * (KEY_LEN + 16) + 1);
    if (!text)
        return 0;
    int n = 0, ok = 0;
    for (int i = 0; i < s_n; i++)
        n += snprintf(text + n, KEY_LEN + 16, "%u %s\n", (unsigned)s_games[i].time, s_games[i].key);
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        ok = sceIoWrite(fd, text, n) == n;
        sceIoClose(fd);
    }
    free(text);
    return ok;
}

static int set(const char *key, u32 time)
{
    Game *g = find(key);
    if (!g) {
        if (s_n >= MAX_GAMES)
            return 0;
        if (s_n == s_cap) {
            int cap = s_cap ? s_cap * 2 : 64;
            Game *n = realloc(s_games, (size_t)cap * sizeof(Game));
            if (!n)
                return 0;
            s_games = n;
            s_cap = cap;
        }
        g = &s_games[s_n++];
        snprintf(g->key, sizeof(g->key), "%s", key);
        g->time = 0;
    }
    if (time <= g->time)
        return 0;
    g->time = time;
    return 1;
}

void lastplayed_load(const char *dir)
{
    s_n = 0;
    snprintf(s_dir, sizeof(s_dir), "%s", dir);
    char path[48];
    file_path(path, sizeof(path));
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0 || st.st_size <= 0 || st.st_size > MAX_FILE)
        return;
    char *text = malloc((size_t)st.st_size + 1);
    SceUID fd = text ? sceIoOpen(path, PSP_O_RDONLY, 0) : -1;
    if (fd < 0) {
        free(text);
        return;
    }
    int got = sceIoRead(fd, text, (unsigned)st.st_size);
    sceIoClose(fd);
    text[got > 0 ? got : 0] = 0;
    for (char *line = text; *line;) {
        char *end = strchr(line, '\n');
        if (!end)
            break; /* a last line without its end was cut off */
        *end = 0;
        if (end > line && end[-1] == '\r')
            end[-1] = 0;
        char *space = strchr(line, ' ');
        if (space && space > line && space[1] && strspn(line, "0123456789") == (size_t)(space - line) && space - line <= 10) {
            unsigned long long t = strtoull(line, NULL, 10);
            *space = 0;
            if (t > 0 && t <= 0xFFFFFFFFull)
                set(space + 1, (u32)t);
        }
        line = end + 1;
    }
    free(text);
}

u32 lastplayed_now(void)
{
    return dt_psp_now(); /* 0 when the clock cannot be read or does not fit */
}

static int game_key(const Entry *e, char *key, int size)
{
    if (e->type == ENTRY_UMD && !e->game_id[0]) {
        key[0] = 0; /* a disc without a disc ID has no key (entry_cover_key() would say "SYSDIR") */
        return 0;
    }
    entry_cover_key(e, key, size);
    return 1;
}

u32 lastplayed_time(const Entry *e)
{
    char key[KEY_LEN];
    if (!s_n || !game_key(e, key, sizeof(key)))
        return 0;
    const Game *g = find(key);
    return g ? g->time : 0;
}

void lastplayed_note_launch(const Entry *e)
{
    char key[KEY_LEN];
    u32 now = lastplayed_now();
    if (!now || !game_key(e, key, sizeof(key)) || !key[0] || strlen(key) >= KEY_LEN)
        return;
    if (set(key, now))
        save();
}
