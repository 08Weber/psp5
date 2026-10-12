/* psp5 – made by Mille Weber */
#ifndef SYSTEM_H
#define SYSTEM_H

/* Lagringen psp5 startades från: "ms0:" eller "ef0:" (PSP Go:s internminne) */
extern char g_root[5];
/* psp5:s egna filer (omslag, cache, psp5.cfg) på den lagringen, t.ex. "ef0:/PSP5" */
extern char g_data_dir[16];
#define PSP5_ROOT g_root
#define PSP5_DATA_DIR g_data_dir

/* Väljer g_root efter var psp5 själv ligger (argv[0]). Anropas först av allt. */
void sys_set_root(const char *self_path);

/* Läser ARK:s konfiguration (om ARK finns). Anropas en gång vid start. */
void sys_init(const char *self_path);
int sys_has_ark(void);
/* ARK:s huvudversion: 4 (ARK-4), 5 (ARK-5) eller 0 (ingen ARK, t.ex. PPSSPP) */
int sys_ark_version(void);
/* PSP-modell (kuKernelGetModel): 0 = PSP-1000, 1 = PSP-2000, 2/3/6/8 = PSP-3000,
 * 4 = PSP Go, 10 = PSP Street, -1 = okänd */
int sys_model(void);
/* 1 om ms0: och ef0: är samma lagring (ARK pekar om ms0: till internminnet) – skanna då bara ms0: */
int sys_ms_is_ef(void);
/* PSP-1000 saknar extra minne och USB-laddning */
int sys_is_psp1000(void);
/* ARK-mappen med avslutande snedstreck, t.ex. "ms0:/PSP/SAVEDATA/ARK_01234/" */
const char *sys_ark_dir(void);
/* 1 om psp5 körs som ARK:s launcher (ARK-mappen/VBOOT.PBP) */
int sys_is_launcher(void);
/* Slår på launcherläget igen om psp5 pausade det för att gå till XMB.
 * Anropas först när psp5 har startat klart. */
void sys_resume_launcher(void);

void sys_exit_xmb(void);
void sys_restart(void);
void sys_suspend(void);
void sys_shutdown(void);
/* Startar ARK:s återställningsmeny. Returnerar bara om det misslyckades. */
void sys_recovery(void);

int sys_usb_start(void);
void sys_usb_stop(void);
/* 0 = av, 1 = aktiv men ingen kabel, 2 = ansluten till dator */
int sys_usb_state(void);

#endif
