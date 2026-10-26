/* psp5 – made by 08Weber */
#ifdef PSP5_TEST
/* Skriptat testläge för PPSSPP: trycker knappar och sparar skärmdumpar
 * utan att någon behöver använda datorns tangentbord. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "testrun.h"

#define MAX_STEPS 128

typedef struct {
    char op;        /* 'w' vänta, 'p' tryck, 'h' håll, 's' skärmdump, 'z' somna, 'r' vakna, 'v' volym, 'c' laddning */
    int frames;
    u32 button;
    char name[32];
} Step;

static Step s_steps[MAX_STEPS];
static int s_count, s_pos, s_wait, s_hold;
static int s_active;
static int s_resume, s_sleep;
static int s_volume = -1, s_charging = -1, s_battery = -1; /* låtsade värden, -1 = riktiga */

static u32 button_by_name(const char *n)
{
    static const struct { const char *name; u32 b; } map[] = {
        { "cross", PSP_CTRL_CROSS }, { "circle", PSP_CTRL_CIRCLE }, { "triangle", PSP_CTRL_TRIANGLE },
        { "square", PSP_CTRL_SQUARE }, { "up", PSP_CTRL_UP }, { "down", PSP_CTRL_DOWN },
        { "left", PSP_CTRL_LEFT }, { "right", PSP_CTRL_RIGHT }, { "l", PSP_CTRL_LTRIGGER },
        { "r", PSP_CTRL_RTRIGGER }, { "start", PSP_CTRL_START }, { "select", PSP_CTRL_SELECT },
    };
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (!strcasecmp(n, map[i].name))
            return map[i].b;
    return 0;
}

void testrun_init(void)
{
    static char buf[4096];
    SceUID fd = sceIoOpen("ms0:/PSP5/test.txt", PSP_O_RDONLY, 0);
    if (fd < 0)
        return;
    int len = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (len <= 0)
        return;
    buf[len] = 0;
    sceIoMkdir("ms0:/PSP5/shots", 0777);

    char *save = NULL;
    for (char *line = strtok_r(buf, "\r\n", &save); line && s_count < MAX_STEPS; line = strtok_r(NULL, "\r\n", &save)) {
        char op[16], arg[32];
        if (sscanf(line, "%15s %31s", op, arg) != 2)
            continue;
        Step *s = &s_steps[s_count];
        memset(s, 0, sizeof(*s));
        if (!strcmp(op, "wait")) {
            s->op = 'w';
            s->frames = atoi(arg);
        } else if (!strcmp(op, "press")) {
            s->op = 'p';
            s->button = button_by_name(arg);
        } else if (!strcmp(op, "hold")) { /* hold right:60 – håll knappen i 60 bildrutor */
            s->op = 'h';
            char *colon = strchr(arg, ':');
            s->frames = colon ? atoi(colon + 1) : 30;
            if (colon)
                *colon = 0;
            s->button = button_by_name(arg);
        } else if (!strcmp(op, "volume")) { /* volume N – N = volym 0–30, +256 = ljudet av */
            s->op = 'v';
            s->frames = atoi(arg);
        } else if (!strcmp(op, "battery")) { /* battery N – batteriet visar N % */
            s->op = 'b';
            s->frames = atoi(arg);
        } else if (!strcmp(op, "charging")) {
            s->op = 'c';
            s->frames = atoi(arg);
        } else if (!strcmp(op, "sleep")) {
            s->op = 'z';
        } else if (!strcmp(op, "resume")) {
            s->op = 'r';
        } else if (!strcmp(op, "shot")) {
            s->op = 's';
            snprintf(s->name, sizeof(s->name), "%s", arg);
        } else {
            continue;
        }
        s_count++;
    }
    s_active = s_count > 0;
}

u32 testrun_buttons(u32 real)
{
    if (!s_active)
        return real;
    if (s_hold > 0) { /* knappen hålls i två bildrutor */
        s_hold--;
        return s_steps[s_pos - 1].button;
    }
    if (s_wait > 0) {
        s_wait--;
        return 0;
    }
    while (s_pos < s_count) {
        Step *s = &s_steps[s_pos];
        if (s->op == 'w') {
            s_pos++;
            s_wait = s->frames;
            return 0;
        }
        if (s->op == 'p' || s->op == 'h') {
            s_pos++;
            s_hold = s->op == 'h' && s->frames > 1 ? s->frames - 1 : 1;
            return s->button;
        }
        if (s->op == 'v' || s->op == 'c' || s->op == 'b') {
            s_pos++;
            if (s->op == 'v')
                s_volume = s->frames;
            else if (s->op == 'c')
                s_charging = s->frames;
            else
                s_battery = s->frames;
            continue;
        }
        if (s->op == 'r' || s->op == 'z') {
            s_pos++;
            if (s->op == 'r')
                s_resume = 1;
            else
                s_sleep = 1;
            return 0;
        }
        return 0; /* 's' hanteras i testrun_frame */
    }
    return 0;
}

int testrun_volume(void) { return s_volume; }
int testrun_charging(void) { return s_charging; }
int testrun_battery(void) { return s_battery; }

int testrun_take_sleep(void)
{
    int r = s_sleep;
    s_sleep = 0;
    return r;
}

int testrun_take_resume(void)
{
    int r = s_resume;
    s_resume = 0;
    return r;
}

/* Skärmdump: lämna NAMN.req och vänta tills testskriptet på datorn har fotograferat
 * fönstret och tagit bort filen (PPSSPP skriver inte tillbaka bildminnet). */
static int s_requested, s_poll;

void testrun_frame(const void *framebuf)
{
    if (!s_active || s_wait > 0 || s_hold > 0 || s_pos >= s_count || s_steps[s_pos].op != 's')
        return;
    char path[80];
    snprintf(path, sizeof(path), "ms0:/PSP5/shots/%s.req", s_steps[s_pos].name);
    if (!s_requested) {
        SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
        if (fd >= 0)
            sceIoClose(fd);
        s_requested = 1;
        s_poll = 0;
        return;
    }
    if (++s_poll % 10)
        return;
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0) { /* fotograferad */
        s_requested = 0;
        s_pos++;
    }
}
#endif
