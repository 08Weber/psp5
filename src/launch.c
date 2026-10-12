/* psp5 – made by Mille Weber */
/* Startar spel på samma sätt som ARK-5:s arkMenu
 * (Launcher-Menus/arkMenu/src/iso.cpp, eboot.cpp och umd.cpp). */
#include <pspkernel.h>
#include <pspumd.h>
#include <psploadexec_kernel.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <string.h>
#include <stdio.h>
#include "launch.h"
#include "iso.h"

#define UMD_DRIVER 0 /* MODE_UMD: den riktiga skivan */
#define PSN_DRIVER 0
#define ISO_DRIVER 3

#define HOMEBREW_RUNLEVEL  0x141
#define ISO_RUNLEVEL       0x123
#define ISO_PBOOT_RUNLEVEL 0x124
#define POPS_RUNLEVEL      0x144
/* samma sak från PSP Go:s internminne (ef0:). Homebrew startas som från ms0: – arkMenu
 * använder 0x152 bara med sin inställning redirect_ms0. */
#define ISO_RUNLEVEL_GO       0x125
#define ISO_PBOOT_RUNLEVEL_GO 0x126
#define POPS_RUNLEVEL_GO      0x155
#define UMD_RUNLEVEL       0x120
#define UMD_UPDATE_RUNLEVEL 0x160 /* skiva med uppdatering (PBOOT.PBP) på minneskortet */

#define UMD_EBOOT_BIN "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN"
#define UMD_EBOOT_OLD "disc0:/PSP_GAME/SYSDIR/EBOOT.OLD"
#define UMD_BOOT_BIN  "disc0:/PSP_GAME/SYSDIR/BOOT.BIN"

/* LoadExec läser sökvägarna efter att vi anropat den – håll dem statiska. */
static char s_path[256];
static char s_argp[256];

int launch_supported(void)
{
    /* sctrlSEGetVersion ger huvudversionen (4 eller 5). sctrlHENGetVersion ger bara
     * mellanversionen, som är 0 på t.ex. ARK 5.0.x – den räcker inte ensam.
     * Utan ARK (PPSSPP) är funktionerna inte länkade och ger 0 eller ett felvärde. */
    return sctrlSEGetVersion() > 0 || sctrlHENGetVersion() > 0;
}

static int file_exists(const char *path)
{
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
}

/* spelet ligger i PSP Go:s internminne – ARK startar det då med en egen apitype */
static int on_ef0(void)
{
    return !strncmp(s_path, "ef0:", 4);
}

/* Installerad uppdatering i ms0: eller ef0:/PSP/GAME/<DISC_ID>/<name>? (som arkMenu) */
static int installed_file(const char *game_id, const char *name, char *out, int out_size)
{
    if (!game_id[0])
        return 0;
    static const char *devs[] = { "ms0:", "ef0:" };
    for (int i = 0; i < 2; i++) {
        snprintf(out, out_size, "%s/PSP/GAME/%s/%s", devs[i], game_id, name);
        if (file_exists(out))
            return 1;
    }
    return 0;
}

static void launch_iso(const char *game_id)
{
    struct SceKernelLoadExecVSHParam param;
    int apitype = on_ef0() ? ISO_RUNLEVEL_GO : ISO_RUNLEVEL;

    if (installed_file(game_id, "PBOOT.PBP", s_argp, sizeof(s_argp))) {
        apitype = on_ef0() ? ISO_PBOOT_RUNLEVEL_GO : ISO_PBOOT_RUNLEVEL;
    } else {
        IsoReader r;
        IsoFile f;
        u32 magic = 0;
        strcpy(s_argp, UMD_EBOOT_BIN);
        if (iso_open(&r, s_path) >= 0) {
            if (iso_find(&r, "PSP_GAME/SYSDIR/EBOOT.OLD", &f)) {
                strcpy(s_argp, UMD_EBOOT_OLD);
            } else if (iso_find(&r, "PSP_GAME/SYSDIR/BOOT.BIN", &f) && f.size >= 4) {
                iso_read(&r, &magic, sizeof(magic), f.offset);
                if (magic != 0)
                    strcpy(s_argp, UMD_BOOT_BIN);
            }
            iso_close(&r);
        }
    }

    memset(&param, 0, sizeof(param));
    param.size = sizeof(param);
    param.key = "umdemu";
    param.argp = s_argp;
    param.args = strlen(s_argp) + 1;
    sctrlSESetDiscType(PSP_UMD_TYPE_GAME);
    sctrlSESetBootConfFileIndex(ISO_DRIVER);
    sctrlSESetUmdFile(s_path);
    sctrlKernelLoadExecVSHWithApitype(apitype, s_path, &param);
}

static void launch_umd(const char *game_id)
{
    struct SceKernelLoadExecVSHParam param;
    int apitype = UMD_UPDATE_RUNLEVEL;
    if (!installed_file(game_id, "PBOOT.PBP", s_path, sizeof(s_path))) {
        apitype = UMD_RUNLEVEL;
        strcpy(s_path, UMD_EBOOT_BIN);
    }

    memset(&param, 0, sizeof(param));
    param.size = sizeof(param);
    param.key = "game";
    param.argp = s_path;
    param.args = strlen(s_path) + 1;
    sctrlSESetDiscType(PSP_UMD_TYPE_GAME);
    sctrlSESetBootConfFileIndex(UMD_DRIVER);
    sctrlSESetUmdFile("");
    sctrlKernelLoadExecVSHWithApitype(apitype, s_path, &param);
}

static void launch_homebrew(void)
{
    struct SceKernelLoadExecVSHParam param;
    memset(&param, 0, sizeof(param));
    param.key = "game";
    param.argp = s_path;
    param.args = strlen(s_path) + 1;
    sctrlKernelLoadExecVSHWithApitype(HOMEBREW_RUNLEVEL, s_path, &param);
}

static void launch_psn(void)
{
    struct SceKernelLoadExecVSHParam param;
    int apitype = on_ef0() ? ISO_RUNLEVEL_GO : ISO_RUNLEVEL;

    strcpy(s_argp, UMD_EBOOT_BIN);
    char *slash = strrchr(s_path, '/');
    if (slash) {
        char pboot[256];
        snprintf(pboot, sizeof(pboot), "%.*s/PBOOT.PBP", (int)(slash - s_path), s_path);
        if (file_exists(pboot)) {
            strcpy(s_argp, pboot);
            apitype = on_ef0() ? ISO_PBOOT_RUNLEVEL_GO : ISO_PBOOT_RUNLEVEL;
        }
    }

    memset(&param, 0, sizeof(param));
    param.key = "umdemu";
    param.argp = s_argp;
    param.args = strlen(s_argp) + 1;
    sctrlSESetBootConfFileIndex(PSN_DRIVER);
    sctrlSESetUmdFile("");
    sctrlKernelLoadExecVSHWithApitype(apitype, s_path, &param);
}

static void launch_pops(void)
{
    struct SceKernelLoadExecVSHParam param;
    memset(&param, 0, sizeof(param));
    param.key = "pops";
    param.argp = s_path;
    param.args = strlen(s_path) + 1;
    sctrlKernelLoadExecVSHWithApitype(on_ef0() ? POPS_RUNLEVEL_GO : POPS_RUNLEVEL, s_path, &param);
}

int launch_entry(const Entry *e)
{
    char game_id[sizeof(e->game_id)];
    EntryType type = e->type;
    strcpy(s_path, e->path);
    strcpy(game_id, e->game_id);

    if (!launch_supported())
        return -1;

    switch (type) {
    case ENTRY_ISO:      launch_iso(game_id); break;
    case ENTRY_HOMEBREW: launch_homebrew();   break;
    case ENTRY_PSN:      launch_psn();        break;
    case ENTRY_POPS:     launch_pops();       break;
    case ENTRY_UMD:      launch_umd(game_id); break;
    }
    return -2; /* LoadExec returnerar bara om något gick fel */
}
