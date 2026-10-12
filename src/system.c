/* psp5 – made by Mille Weber */
/* Systemfunktioner som arkMenu har: USB, ström, XMB och återställningsmeny.
 * Fungerar på både ARK-4 och ARK-5 – versionen känns av vid start. */
#include <pspkernel.h>
#include <pspusb.h>
#include <pspusbstor.h>
#include <psploadexec_kernel.h>
#include <kubridge.h>
#include <systemctrl.h>
#include <systemctrl_ark.h>
#include <systemctrl_se.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "system.h"
#include "launch.h"
#include "arkcfg.h"

#define USBSTOR_EF_DRIVERNAME "USBStorEFlash_Driver"
/* finns när launcherläget är pausat för att psp5 gick till XMB */
static const char *paused_mark(void)
{
    static char path[32];
    snprintf(path, sizeof(path), "%s/launcher.paused", g_data_dir);
    return path;
}

int scePowerRequestSuspend(void);
int scePowerRequestColdReset(int unk);
int scePowerRequestStandby(void);

static int exists(const char *path);

/* ARK-4: sctrlArkGetConfig ligger i SystemCtrlForUser (src/ark4stub.S) */
ARKConfig *sctrlArkGetConfig_ark4(ARKConfig *conf);

static ARKConfig s_conf;
static int s_ark;        /* 4, 5 eller 0 = ingen ARK */
static int s_model = -1; /* kuKernelGetModel(), -1 = okänd */
static int s_has_ark;
static int s_is_launcher;
static int s_usb_on;
static char s_ark_dir[ARK_PATH_SIZE];
static int s_ms_is_ef; /* ARK låter ms0: peka på PSP Go:s internminne */

char g_root[5] = "ms0:";
char g_data_dir[16] = "ms0:/PSP5";

void sys_set_root(const char *self_path)
{
    /* som app i ef0:/PSP/GAME/PSP5 eller som launcher i ef0:/PSP/SAVEDATA/ARK_01234 */
    if (self_path && !strncasecmp(self_path, "ef0:", 4))
        snprintf(g_root, sizeof(g_root), "ef0:");
    snprintf(g_data_dir, sizeof(g_data_dir), "%s/PSP5", g_root);
}

void sys_init(const char *self_path)
{
    char path[200];
    snprintf(s_ark_dir, sizeof(s_ark_dir), "%s/PSP/SAVEDATA/ARK_01234/", g_root);
    /* ARK:s funktioner finns bara med CFW – inte i t.ex. PPSSPP */
    if (launch_supported()) {
        int major = sctrlSEGetVersion();
        s_ark = major == 4 ? 4 : 5; /* ARK-4 ger 4, ARK-5 ger 5 */
        s_model = kuKernelGetModel();
        s_ms_is_ef = sctrlKernelMsIsEf() == 1;
        memset(&s_conf, 0, sizeof(s_conf));
        /* anropa bara den variant som finns i den ARK som körs */
        if (s_ark == 4)
            sctrlArkGetConfig_ark4(&s_conf);
#ifndef PSP5_ARK4
        else
            sctrlArkGetConfig(&s_conf);
#endif
        char settings[200], vboot[200];
        snprintf(settings, sizeof(settings), "%sSETTINGS.TXT", s_ark_dir);
        snprintf(vboot, sizeof(vboot), "%sVBOOT.PBP", s_ark_dir);
        snprintf(path, sizeof(path), "%s/SEPLUGINS/SETTINGS.TXT", g_root);
        if (IS_ARK_CONFIG(&s_conf) && s_conf.arkpath[0]) {
            s_has_ark = 1;
            snprintf(s_ark_dir, sizeof(s_ark_dir), "%s", s_conf.arkpath);
        } else if (exists(settings) || exists(vboot)) {
            s_has_ark = 1; /* konfigurationen gick inte att läsa – standardmappen finns */
        } else if (s_ark == 4 && exists(path)) {
            s_has_ark = 1; /* ARK-4 utan ARK-mapp använder <lagring>/SEPLUGINS/ */
            snprintf(s_ark_dir, sizeof(s_ark_dir), "%s/SEPLUGINS/", g_root);
        }
    }
    g_arkcfg_ark4 = s_ark == 4;
    snprintf(path, sizeof(path), "%sVBOOT.PBP", s_ark_dir);
    s_is_launcher = self_path && !strcasecmp(self_path, path);
}

int sys_has_ark(void) { return s_has_ark; }
int sys_ark_version(void) { return s_ark; }
int sys_model(void) { return s_model; }
int sys_ms_is_ef(void) { return s_ms_is_ef; }
int sys_is_psp1000(void) { return s_model == 0; }
const char *sys_ark_dir(void) { return s_ark_dir; }
int sys_is_launcher(void) { return s_is_launcher; }

static void settings_path(char *path, int size)
{
    snprintf(path, size, "%sSETTINGS.TXT", s_ark_dir);
}

/* ARK läser SETTINGS.TXT vid varje omstart och slår då på launcherläget igen, så
 * en utgång till XMB hamnar annars i psp5 direkt. Pausa läget i filen och lämna en
 * markering – nästa gång psp5 startas (från XMB) slås det på igen. */
static void pause_launcher(void)
{
    char path[200];
    settings_path(path, sizeof(path));
    if (!arkcfg_load(path) || !g_cfw.launcher)
        return;
    g_cfw.launcher = 0;
    if (!arkcfg_save(path))
        return;
    SceUID fd = sceIoOpen(paused_mark(), PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0)
        sceIoClose(fd);
}

void sys_resume_launcher(void)
{
    if (!s_has_ark || !exists(paused_mark()))
        return;
    char path[200];
    settings_path(path, sizeof(path));
    if (arkcfg_load(path)) {
        g_cfw.launcher = 1;
        if (!arkcfg_save(path))
            return; /* försök igen nästa start */
    }
    sceIoRemove(paused_mark());
}

void sys_exit_xmb(void)
{
    if (!s_has_ark) {
        sceKernelExitGame();
        return;
    }
    pause_launcher();
    if (s_ark >= 5) {
        /* stäng också av läget i minnet, så att inget i den här sessionen startar launchern.
         * (ARK-4 har en annan SEConfig-layout och läser ändå om SETTINGS.TXT vid omstarten.) */
        SEConfigARK se;
        memset(&se, 0, sizeof(se));
        if (sctrlSEGetConfigEx((SEConfig *)&se, sizeof(se)) >= 0 && se.magic == ARK_CONFIG_MAGIC) {
            se.launcher_mode = 0;
            sctrlSESetConfigEx((SEConfig *)&se, sizeof(se));
        }
#ifndef PSP5_ARK4
        if (IS_ARK_CONFIG(&s_conf)) { /* skriv aldrig tillbaka en konfiguration som inte gick att läsa */
            s_conf.launcher[0] = 0;
            sctrlArkSetConfig(&s_conf);
        }
#endif
    }
    sctrlKernelExitVSH(NULL);
}

void sys_restart(void) { scePowerRequestColdReset(0); }
void sys_suspend(void) { scePowerRequestSuspend(); }
void sys_shutdown(void) { scePowerRequestStandby(); }

static int exists(const char *path)
{
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
}

void sys_recovery(void)
{
    if (!s_has_ark)
        return;
    char path[200];
    /* som arkMenu: RECOVERY.PBP om den finns, annars den klassiska PRX-menyn */
    snprintf(path, sizeof(path), "%sRECOVERY.PBP", s_ark_dir);
    if (exists(path)) {
        struct SceKernelLoadExecVSHParam param;
        memset(&param, 0, sizeof(param));
        param.args = strlen(path) + 1;
        param.argp = path;
        param.key = "game";
        sctrlKernelLoadExecVSHWithApitype(0x141, path, &param);
        return;
    }
    snprintf(path, sizeof(path), "%sRECOVERY.PRX", s_ark_dir);
    if (!exists(path))
        snprintf(path, sizeof(path), "%s", RECOVERY_PRX_FLASH);
    SceUID mod = kuKernelLoadModule(path, 0, NULL);
    if (mod >= 0 && sceKernelStartModule(mod, strlen(path) + 1, path, NULL, NULL) >= 0) {
        for (;;)
            sceKernelDelayThread(1000000); /* återställningsmenyn tar över */
    }
}

int sys_usb_start(void)
{
    static const char *mods[] = {
        "flash0:/kd/semawm.prx",     "flash0:/kd/usbstor.prx",      "flash0:/kd/usbstormgr.prx",
        "flash0:/kd/usbstorms.prx",  "flash0:/kd/usbstoreflash.prx", "flash0:/kd/usbstorboot.prx",
    };
    static int loaded;
    if (s_usb_on || !s_has_ark)
        return s_usb_on;
    if (!loaded) {
        for (unsigned i = 0; i < sizeof(mods) / sizeof(mods[0]); i++) {
            SceUID mid = kuKernelLoadModule(mods[i], 0, NULL);
            if (mid >= 0)
                sceKernelStartModule(mid, 0, NULL, NULL, NULL);
        }
        loaded = 1;
    }
    sceUsbStart(PSP_USBBUS_DRIVERNAME, 0, 0);
    sceUsbStart(PSP_USBSTOR_DRIVERNAME, 0, 0);
    sceUsbStart(USBSTOR_EF_DRIVERNAME, 0, 0);
    s_usb_on = sceUsbActivate(0x1c8) >= 0;
    return s_usb_on;
}

void sys_usb_stop(void)
{
    if (!s_usb_on)
        return;
    sceUsbDeactivate(0x1c8);
    sceUsbStop(USBSTOR_EF_DRIVERNAME, 0, 0);
    sceUsbStop(PSP_USBSTOR_DRIVERNAME, 0, 0);
    sceUsbStop(PSP_USBBUS_DRIVERNAME, 0, 0);
    s_usb_on = 0;
}

int sys_usb_state(void)
{
    if (!s_usb_on)
        return 0;
    return (sceUsbGetState() & PSP_USB_CONNECTION_ESTABLISHED) ? 2 : 1;
}
