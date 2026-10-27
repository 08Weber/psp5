/* psp5 – made by 08Weber */
/* Systemfunktioner som arkMenu har: USB, ström, XMB och återställningsmeny.
 * Fungerar på både ARK-4 och ARK-5 – versionen känns av vid start. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <psputility.h>
#include <pspusb.h>
#include <pspusbstor.h>
#include <psploadexec_kernel.h>
#include <kubridge.h>
#include <systemctrl.h>
#include <systemctrl_ark.h>
#include <systemctrl_se.h>
#include <stdio.h>
#include <stdlib.h>
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

static int dir_exists(const char *path)
{
    SceUID d = sceIoDopen(path);
    if (d < 0)
        return 0;
    sceIoDclose(d);
    return 1;
}

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
    if (launch_supported() && sctrlKernelMsIsEf() == 1) {
        if (kuKernelInitApitype() != 0x141 && self_path && self_path[0]) {
            /* Startad från XMB på PSP Go:s internminne (apitype 0x152): Sony låter då ms0:
             * peka på ef0: och laddar inte minneskortsdrivrutinen alls (issue #17, #20, #22).
             * Starta om direkt i läge 0x141 (som VBOOT.PBP), där både ms0: och ef0: finns. */
            char ef_self[256];
            if (!strncasecmp(self_path, "ms0:", 4))
                snprintf(ef_self, sizeof(ef_self), "ef0:%s", self_path + 4);
            else
                snprintf(ef_self, sizeof(ef_self), "%s", self_path);
            if (exists(ef_self)) {
                struct SceKernelLoadExecVSHParam param;
                memset(&param, 0, sizeof(param));
                param.size = sizeof(param);
                param.args = strlen(ef_self) + 1;
                param.argp = ef_self;
                param.key = "game";
                sctrlKernelLoadExecVSHWithApitype(0x141, ef_self, &param);
            }
        }
        snprintf(g_root, sizeof(g_root), "ef0:");
    }
    /* som app i ef0:/PSP/GAME/PSP5 eller som launcher i ef0:/PSP/SAVEDATA/ARK_01234 */
    if (self_path && !strncasecmp(self_path, "ef0:", 4))
        snprintf(g_root, sizeof(g_root), "ef0:");
    snprintf(g_data_dir, sizeof(g_data_dir), "%s/PSP5", g_root);
}

#ifdef PSP5_TEST
/* Testbygget i PPSSPP (som saknar ARK): <data>/fake.txt med t.ex. "model=4 ark=4" låtsas vara en
 * PSP Go med ARK-4, så att inställningarna för varje modell och ARK går att se. */
static void fake_device(void)
{
    char path[64], buf[64] = "";
    snprintf(path, sizeof(path), "%s/fake.txt", g_data_dir);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return;
    int n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    buf[n > 0 ? n : 0] = 0;
    const char *p;
    if ((p = strstr(buf, "model=")))
        s_model = atoi(p + 6);
    if ((p = strstr(buf, "ark=")))
        s_ark = atoi(p + 4);
}
#endif

static void resolve_ark_dir(void)
{
    char cand[3][ARK_PATH_SIZE];
    int n = 0;
    snprintf(cand[n++], ARK_PATH_SIZE, "%s/PSP/SAVEDATA/ARK_01234/", g_root);
    if (s_model == 4 && strcasecmp(g_root, "ef0:") != 0)
        snprintf(cand[n++], ARK_PATH_SIZE, "ef0:/PSP/SAVEDATA/ARK_01234/");
    if (!s_ms_is_ef && strcasecmp(g_root, "ms0:") != 0)
        snprintf(cand[n++], ARK_PATH_SIZE, "ms0:/PSP/SAVEDATA/ARK_01234/");

    for (int i = 0; i < n; i++) {
        if (dir_exists(cand[i])) {
            snprintf(s_ark_dir, sizeof(s_ark_dir), "%s", cand[i]);
            if (IS_ARK_CONFIG(&s_conf))
                snprintf(s_conf.arkpath, sizeof(s_conf.arkpath), "%s", cand[i]);
            return;
        }
    }
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
            /* Om ARK föll tillbaka till SEPLUGINS men ARK_01234 finns på ms0:/ef0:, använd ARK_01234 (issue #22, #23) */
            if (strstr(s_ark_dir, "SEPLUGINS") || !dir_exists(s_ark_dir))
                resolve_ark_dir();
        } else if (exists(settings) || exists(vboot)) {
            s_has_ark = 1; /* konfigurationen gick inte att läsa – standardmappen finns */
        } else {
            resolve_ark_dir();
            if (dir_exists(s_ark_dir)) {
                s_has_ark = 1;
            } else if (s_ark == 4 && exists(path)) {
                s_has_ark = 1; /* ARK-4 utan ARK-mapp använder <lagring>/SEPLUGINS/ */
                snprintf(s_ark_dir, sizeof(s_ark_dir), "%s/SEPLUGINS/", g_root);
            }
        }
    }
#ifdef PSP5_TEST
    fake_device();
#endif
    g_arkcfg_ark4 = s_ark == 4;
    snprintf(path, sizeof(path), "%sVBOOT.PBP", s_ark_dir);
    const char *base = self_path ? strrchr(self_path, '/') : NULL;
    s_is_launcher = (self_path && !strcasecmp(self_path, path)) ||
                    (base && !strcasecmp(base + 1, "VBOOT.PBP"));

    /* ARK-5 sätter klockfrekvens (överklockning) och ef0:/ms0:-cache först när en modul
     * startas efter att kärnan har nått systemstatus 0x20000 (som arkMenu gör vid start).
     * Trigga det direkt här innan våra trådar och filskanningar startar (issue #19). */
    if (s_has_ark) {
        int r = sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
        if (r >= 0)
            sceUtilityUnloadModule(PSP_MODULE_AV_AVCODEC);
    }
}

int sys_has_ark(void) { return s_has_ark; }
int sys_ark_version(void) { return s_ark; }
int sys_model(void) { return s_model; }
int sys_ms_is_ef(void) { return s_ms_is_ef; }
int sys_is_psp1000(void) { return s_model == 0; }
const char *sys_ark_dir(void)
{
    if (strstr(s_ark_dir, "SEPLUGINS"))
        resolve_ark_dir(); /* om användaren nyss skapade ARK_01234 via USB eller filhanteraren (issue #23) */
    return s_ark_dir;
}
int sys_is_launcher(void) { return s_is_launcher; }

static void settings_path(char *path, int size)
{
    snprintf(path, size, "%sSETTINGS.TXT", sys_ark_dir());
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
    /* Pausa bara launcherläget om psp5 faktiskt körs som launcher (VBOOT.PBP).
     * Om användaren slog på "Starta psp5 vid uppstart" inifrån appen i PSP/GAME/PSP5
     * och sedan avslutar, ska inställningen inte slås av igen (issue #23). */
    if (s_is_launcher) {
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
    }
    sceKernelDelayThread(50000); /* låt minneskortets/ef0:-cachen skriva klart (issue #19, #23) */
    sctrlKernelExitVSH(NULL);
}

void sys_restart(void)
{
    sceKernelDelayThread(50000);
    scePowerRequestColdReset(0);
}

void sys_suspend(void) { scePowerRequestSuspend(); }

void sys_shutdown(void)
{
    sceKernelDelayThread(50000);
    scePowerRequestStandby();
}

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

static int kcall(void *f, u32 a1, u32 a2)
{
    KernelCallArg a;
    memset(&a, 0, sizeof(a));
    a.arg1 = a1;
    a.arg2 = a2;
    return kuKernelCall(f, &a) < 0 ? -1 : (int)a.ret1;
}

int sys_usb_start(void)
{
    static const char *mods[] = {
        "flash0:/kd/usb.prx",        "flash0:/kd/semawm.prx",       "flash0:/kd/usbstor.prx",
        "flash0:/kd/usbstormgr.prx", "flash0:/kd/usbstorms.prx",    "flash0:/kd/usbstoreflash.prx",
        "flash0:/kd/usbstorboot.prx",
    };
    static int loaded;
    if (s_usb_on || !s_has_ark)
        return s_usb_on;
    /* ARK:s USB-laddning kan låsa USB-anslutningen på PSP Go om kabeln redan sitter i –
     * stäng av USB-laddningen innan drivrutinerna aktiveras (issue #20). */
    void *disable_charge = (void *)sctrlHENFindFunction("scePower_Service", "scePower_driver", 0x90285886);
    if (disable_charge)
        kcall(disable_charge, 0, 0);
    if (!loaded) {
        for (unsigned i = 0; i < sizeof(mods) / sizeof(mods[0]); i++) {
            if (s_model != 4 && strstr(mods[i], "usbstoreflash"))
                continue;
            SceUID mid = kuKernelLoadModule(mods[i], 0, NULL);
            if (mid >= 0)
                sceKernelStartModule(mid, 0, NULL, NULL, NULL);
        }
        loaded = 1;
    }
    if (sceUsbGetState() & PSP_USB_ACTIVATED) {
        sceUsbDeactivate(0x1c8);
        sceKernelDelayThread(10000);
    }
    sceUsbStart(PSP_USBBUS_DRIVERNAME, 0, 0);
    sceUsbStart(PSP_USBSTOR_DRIVERNAME, 0, 0);
    if (s_model == 4)
        sceUsbStart(USBSTOR_EF_DRIVERNAME, 0, 0);
    sceUsbstorBootSetCapacity(0x800000);
    /* som arkMenu: resultatet ignoreras – ett fel här stängde USB-läget direkt (issue #10) */
    sceUsbActivate(0x1c8);
    s_usb_on = 1;
    return s_usb_on;
}

void sys_usb_stop(void)
{
    if (!s_usb_on)
        return;
    sceUsbDeactivate(0x1c8);
    if (s_model == 4)
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

/* --- HOME som Control Center, och skärmen av/på ---
 * Som ARK:s egen meny (arkMenu): kärnfunktionerna hittas med sctrlHENFindFunction (ARK
 * översätter de gamla NID:erna till 6.60/6.61) och anropas i kärnläge med kuKernelCall.
 * I användarläge döljer systemet HOME-knappen. */
#define NID_CTRL_PEEK      0x3A622550 /* sceCtrlPeekBufferPositive */
#define NID_HOME_POPUP     0x5595A71A /* sceImposeSetHomePopup */
#define NID_DISP_ENABLE    0x432D133F /* sceDisplayEnable */
#define NID_DISP_DISABLE   0x681EE6A7 /* sceDisplayDisable */
#define NID_DISP_SET_LIGHT 0x9E3C6DC6 /* sceDisplaySetBrightness(nivå, 0) */
#define NID_DISP_GET_LIGHT 0x31C4BAA8 /* sceDisplayGetBrightness(&nivå, &okänt) */
#define HOME_BIT           0x10000    /* PSP_CTRL_HOME */
#define WATCHDOG_US        3000000    /* menyn har stått still så länge: släpp HOME till systemet */

static void *s_peek, *s_popup, *s_disp_on, *s_disp_off, *s_light_set, *s_light_get;
static volatile int s_popup_off;  /* 1 = systemets HOME-dialog är avstängd */
static volatile u32 s_heartbeat;  /* sceKernelGetSystemTimeLow() när menyn senast läste knapparna */
static int s_light = -1;          /* ljusstyrkan innan skärmen släcktes */

static void set_popup(int on)
{
    if (s_popup && kcall(s_popup, on, 0) >= 0)
        s_popup_off = !on;
}

/* Hänger sig psp5 med dialogen avstängd skulle HOME inte gå att använda – vakthunden slår på
 * den igen när menyn inte har läst knapparna på en stund (även under långa inläsningar). */
static int home_watchdog(SceSize args, void *argp)
{
    for (;;) {
        sceKernelDelayThread(500000);
        if (s_popup_off && sceKernelGetSystemTimeLow() - s_heartbeat > WATCHDOG_US)
            set_popup(1);
    }
    return 0;
}

int sys_home_init(void)
{
    if (!s_has_ark || s_peek)
        return s_peek != NULL;
    s_peek = (void *)sctrlHENFindFunction("sceController_Service", "sceCtrl_driver", NID_CTRL_PEEK);
    s_popup = (void *)sctrlHENFindFunction("sceImpose_Driver", "sceImpose_driver", NID_HOME_POPUP);
    s_disp_on = (void *)sctrlHENFindFunction("sceDisplay_Service", "sceDisplay_driver", NID_DISP_ENABLE);
    s_disp_off = (void *)sctrlHENFindFunction("sceDisplay_Service", "sceDisplay_driver", NID_DISP_DISABLE);
    s_light_set = (void *)sctrlHENFindFunction("sceDisplay_Service", "sceDisplay_driver", NID_DISP_SET_LIGHT);
    s_light_get = (void *)sctrlHENFindFunction("sceDisplay_Service", "sceDisplay_driver", NID_DISP_GET_LIGHT);
    if (!s_peek || !s_popup) {
        s_peek = s_popup = NULL;
        return 0;
    }
    s_heartbeat = sceKernelGetSystemTimeLow();
    int th = sceKernelCreateThread("psp5_home", home_watchdog, 0x10, 0x1000, PSP_THREAD_ATTR_USER, NULL);
    if (th >= 0)
        sceKernelStartThread(th, 0, NULL);
    return 1;
}

u32 sys_home_buttons(void)
{
    if (!s_peek)
        return 0;
    s_heartbeat = sceKernelGetSystemTimeLow();
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    if (kcall(s_peek, (u32)&pad, 1) < 0)
        return 0;
    if (!s_popup_off) {
        /* ta inte över mitt i ett tryck – då skulle både systemets dialog och vår meny öppnas */
        if (!(pad.Buttons & HOME_BIT))
            set_popup(0);
        return 0;
    }
    return pad.Buttons & HOME_BIT;
}

void sys_home_release(void)
{
    if (s_popup_off)
        set_popup(1);
}

int sys_display(int on)
{
    if (!s_disp_on || !s_disp_off)
        return 0;
    if (on) {
        if (s_light_set && s_light >= 0)
            kcall(s_light_set, s_light, 0);
        kcall(s_disp_on, 0, 0);
    } else {
        int level = -1, unk = 0;
        if (s_light_get && kcall(s_light_get, (u32)&level, (u32)&unk) >= 0 && level >= 0)
            s_light = level;
        if (s_light_set && s_light >= 0) /* bara om ljusstyrkan går att ställa tillbaka */
            kcall(s_light_set, 0, 0);
        kcall(s_disp_off, 0, 0);
    }
    return 1;
}
