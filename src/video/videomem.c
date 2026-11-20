/* psp5 authentic – where a video was left (see videomem.h) */
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "videomem.h"
#include "system.h"

#define MAX_VIDEOS 200
#define NAME_MAX_LEN 128
#define MAX_FILE (MAX_VIDEOS * (NAME_MAX_LEN + 32))
#define NEAR_START 5.0f
#define NEAR_END 10.0f

typedef struct {
    char name[NAME_MAX_LEN];
    int len;
    int pos;
} Place;

static Place s_places[MAX_VIDEOS]; /* the newest first */
static int s_n;
static int s_loaded;
static char s_loaded_dir[24];

static void file_path(char *out, int size)
{
    snprintf(out, size, "%s/videomem.txt", PSP5_DATA_DIR);
}

static void load(void)
{
    if (s_loaded && !strcmp(s_loaded_dir, PSP5_DATA_DIR))
        return;
    s_loaded = 1;
    snprintf(s_loaded_dir, sizeof(s_loaded_dir), "%s", PSP5_DATA_DIR);
    s_n = 0;
    char path[48];
    file_path(path, sizeof(path));
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return;
    char *text = malloc(MAX_FILE + 1);
    if (!text) {
        sceIoClose(fd);
        return;
    }
    int got = sceIoRead(fd, text, MAX_FILE);
    sceIoClose(fd);
    if (got < 0)
        got = 0;
    text[got] = 0;
    for (char *line = strtok(text, "\n"); line && s_n < MAX_VIDEOS; line = strtok(NULL, "\n")) {
        int len, pos, used = 0;
        if (sscanf(line, "%d %d %n", &len, &pos, &used) < 2 || used <= 0 || len <= 0 || pos < 0)
            continue;
        char *cr = strchr(line + used, '\r');
        if (cr)
            *cr = 0;
        if (!line[used] || strlen(line + used) >= NAME_MAX_LEN)
            continue;
        snprintf(s_places[s_n].name, NAME_MAX_LEN, "%s", line + used);
        s_places[s_n].len = len;
        s_places[s_n].pos = pos;
        s_n++;
    }
    free(text);
}

static void save(void)
{
    char path[48];
    file_path(path, sizeof(path));
    char *text = malloc((size_t)s_n * (NAME_MAX_LEN + 32) + 1);
    if (!text)
        return;
    int n = 0;
    for (int i = 0; i < s_n; i++)
        n += snprintf(text + n, NAME_MAX_LEN + 32, "%d %d %s\n", s_places[i].len, s_places[i].pos, s_places[i].name);
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, text, n);
        sceIoClose(fd);
    }
    free(text);
}

static int find(const char *name, int len_s)
{
    for (int i = 0; i < s_n; i++)
        if (s_places[i].len == len_s && !strcmp(s_places[i].name, name))
            return i;
    return -1;
}

int videomem_get(const char *name, int len_s, float *pos)
{
    load();
    int i = find(name, len_s);
    if (i < 0)
        return 0;
    *pos = (float)s_places[i].pos;
    return 1;
}

void videomem_forget(const char *name, int len_s)
{
    load();
    int i = find(name, len_s);
    if (i < 0)
        return;
    memmove(s_places + i, s_places + i + 1, (size_t)(s_n - i - 1) * sizeof(Place));
    s_n--;
    save();
}

void videomem_set(const char *name, int len_s, float pos)
{
    if (len_s <= 0 || strlen(name) >= NAME_MAX_LEN)
        return;
    if (pos < NEAR_START || pos > (float)len_s - NEAR_END) { /* not started, or finished */
        videomem_forget(name, len_s);
        return;
    }
    load();
    int i = find(name, len_s);
    if (i < 0) {
        if (s_n < MAX_VIDEOS)
            s_n++;
        i = s_n - 1; /* (the oldest goes when the table is full) */
    }
    memmove(s_places + 1, s_places, (size_t)i * sizeof(Place));
    snprintf(s_places[0].name, NAME_MAX_LEN, "%s", name);
    s_places[0].len = len_s;
    s_places[0].pos = (int)pos;
    save();
}
