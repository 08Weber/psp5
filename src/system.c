/* Systemfunktioner som arkMenu har: USB, ström, XMB och återställningsmeny. */
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
#include "system.h"
#include "launch.h"

#define DEFAULT_ARK_DIR "ms0:/PSP/SAVEDATA/ARK_01234/"
#define USBSTOR_EF_DRIVERNAME "USBStorEFlash_Driver"

int scePowerRequestSuspend(void);
int scePowerRequestColdReset(int unk);
int scePowerRequestStandby(void);

static ARKConfig s_conf;
static int s_has_ark;
static int s_is_launcher;
static int s_usb_on;
static char s_ark_dir[ARK_PATH_SIZE];

void sys_init(const char *self_path)
{
    snprintf(s_ark_dir, sizeof(s_ark_dir), DEFAULT_ARK_DIR);
    /* ARK:s funktioner finns bara med CFW – inte i t.ex. PPSSPP */
    if (launch_supported()) {
        memset(&s_conf, 0, sizeof(s_conf));
        sctrlArkGetConfig(&s_conf);
        if (IS_ARK_CONFIG(&s_conf) && s_conf.arkpath[0]) {
            s_has_ark = 1;
            snprintf(s_ark_dir, sizeof(s_ark_dir), "%s", s_conf.arkpath);
        }
    }
    char launcher[200];
    snprintf(launcher, sizeof(launcher), "%sVBOOT.PBP", s_ark_dir);
    s_is_launcher = self_path && !strcasecmp(self_path, launcher);
}

int sys_has_ark(void) { return s_has_ark; }
const char *sys_ark_dir(void) { return s_ark_dir; }
int sys_is_launcher(void) { return s_is_launcher; }

void sys_exit_xmb(void)
{
    if (!s_has_ark) {
        sceKernelExitGame();
        return;
    }
    /* I launcherläge byter ARK ut varje XMB-start mot launchern. Stäng av läget
     * för den här sessionen (inte i SETTINGS.TXT), så att vi verkligen hamnar i XMB.
     * Vid nästa omstart gäller inställningen igen. */
    SEConfigARK se;
    memset(&se, 0, sizeof(se));
    if (sctrlSEGetConfigEx((SEConfig *)&se, sizeof(se)) >= 0 && se.magic == ARK_CONFIG_MAGIC) {
        se.launcher_mode = 0;
        sctrlSESetConfigEx((SEConfig *)&se, sizeof(se));
    }
    s_conf.launcher[0] = 0;
    sctrlArkSetConfig(&s_conf);
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
