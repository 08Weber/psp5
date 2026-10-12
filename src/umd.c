/* psp5 – made by Mille Weber */
/* UMD-skivan, på samma sätt som ARK-5:s arkMenu (Launcher-Menus/arkMenu/src/umd.cpp
 * och gamemgr.cpp): en tråd tittar efter skivan och läser PARAM.SFO när den har
 * snurrat upp. Bilderna läses senare av art.c direkt från disc0:. */
#include <pspkernel.h>
#include <pspumd.h>
#include <stdlib.h>
#include <string.h>
#include "umd.h"

#define UMD_SFO   UMD_ROOT "PSP_GAME/PARAM.SFO"
#define UMD_ICON0 UMD_ROOT "PSP_GAME/ICON0.PNG"
#define UMD_PIC1  UMD_ROOT "PSP_GAME/PIC1.PNG"
#define MAX_SFO_SIZE (64 * 1024)

#define POLL_US  (300 * 1000)
#define READY_US (500 * 1000) /* väntar i små steg så att umd_stop inte dröjer */
#define READY_TRIES 40        /* 20 s – vissa skivor snurrar upp långsamt */

static SceUID s_thread = -1;
static SceUID s_lock = -1;
static volatile int s_quit;
static volatile int s_gen; /* ökar när skivan byts */
static int s_seen;         /* s_gen vid senaste umd_poll */
static int s_has;
static Entry s_entry;

static u32 file_size(const char *path)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    int size = sceIoLseek32(fd, 0, PSP_SEEK_END);
    sceIoClose(fd);
    return size > 0 ? size : 0;
}

/* Utan PARAM.SFO är det ingen spelskiva (t.ex. UMD Video) */
static int read_disc(Entry *e)
{
    u32 size = file_size(UMD_SFO);
    if (!size || size > MAX_SFO_SIZE)
        return 0;
    u8 *sfo = malloc(size);
    if (!sfo)
        return 0;
    int got = -1;
    SceUID fd = sceIoOpen(UMD_SFO, PSP_O_RDONLY, 0);
    if (fd >= 0) {
        got = sceIoRead(fd, sfo, size);
        sceIoClose(fd);
    }
    if (got != (int)size) {
        free(sfo);
        return 0;
    }

    memset(e, 0, sizeof(*e));
    strcpy(e->path, "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN");
    strcpy(e->title, "UMD");
    e->type = ENTRY_UMD;
    e->file_size = size;
    entry_read_sfo(e, sfo, size);
    free(sfo);

    /* art.c läser bilderna som hela filer på skivan */
    e->icon_size = file_size(UMD_ICON0);
    e->pic_size = file_size(UMD_PIC1);
    return 1;
}

#ifdef PSP5_TEST
static int medium_present(void)
{
    SceIoStat st;
    return sceIoGetstat(UMD_SFO, &st) >= 0;
}

static int drive_ready(void)
{
    return 1;
}
#else
static int medium_present(void)
{
    return sceUmdCheckMedium() > 0;
}

/* Skivan snurrar upp – vänta högst 20 s, men avbryt om psp5 stängs eller skivan tas ut */
static int drive_ready(void)
{
    sceUmdActivate(1, "disc0:");
    for (int t = 0; t < READY_TRIES && !s_quit && medium_present(); t++)
        if (sceUmdWaitDriveStatWithTimer(PSP_UMD_READY, READY_US) >= 0)
            break;
    return !s_quit && (sceUmdGetDriveStat() & PSP_UMD_READY);
}
#endif

static void publish(int has, const Entry *e)
{
    sceKernelWaitSema(s_lock, 1, NULL);
    s_has = has;
    if (has)
        s_entry = *e;
    s_gen++;
    sceKernelSignalSema(s_lock, 1);
}

static int umd_thread(SceSize args, void *argp)
{
    int state = 0; /* 0 = ingen skiva, 1 = spelskiva inläst, 2 = skiva som inte går att visa */
    while (!s_quit) {
        int present = medium_present();
        if (present && state == 0) {
            Entry e;
            if (drive_ready() && read_disc(&e)) {
                publish(1, &e);
                state = 1;
            } else {
                state = 2;
            }
        } else if (!present && state != 0) {
            if (state == 1)
                publish(0, NULL);
            state = 0;
        }
        sceKernelDelayThread(POLL_US);
    }
    return 0;
}

void umd_start(void)
{
#ifdef PSP5_GO
    return; /* PSP Go har ingen UMD-läsare */
#endif
    s_quit = 0;
    s_gen = s_seen = 0;
    s_has = 0;
    s_lock = sceKernelCreateSema("psp5_umd", 0, 1, 1, NULL);
    if (s_lock < 0)
        return;
    s_thread = sceKernelCreateThread("psp5_umd", umd_thread, 0x30, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (s_thread >= 0)
        sceKernelStartThread(s_thread, 0, NULL);
}

void umd_stop(void)
{
    if (s_thread >= 0) {
        s_quit = 1;
        sceKernelWaitThreadEnd(s_thread, NULL);
        sceKernelDeleteThread(s_thread);
        s_thread = -1;
    }
    if (s_lock >= 0) {
        sceKernelDeleteSema(s_lock);
        s_lock = -1;
    }
}

int umd_poll(Entry *e, int *has)
{
    if (s_lock < 0 || s_gen == s_seen)
        return 0;
    sceKernelWaitSema(s_lock, 1, NULL);
    s_seen = s_gen;
    *has = s_has;
    if (s_has)
        *e = s_entry;
    sceKernelSignalSema(s_lock, 1);
    return 1;
}
