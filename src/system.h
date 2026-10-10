/* psp5 – made by Mille Weber */
#ifndef SYSTEM_H
#define SYSTEM_H

/* psp5:s egna filer (omslag, cache, psp5.cfg) */
#define PSP5_DATA_DIR "ms0:/PSP5"

/* Läser ARK:s konfiguration (om ARK finns). Anropas en gång vid start. */
void sys_init(const char *self_path);
int sys_has_ark(void);
/* ARK-mappen med avslutande snedstreck, t.ex. "ms0:/PSP/SAVEDATA/ARK_01234/" */
const char *sys_ark_dir(void);
/* 1 om psp5 körs som ARK:s launcher (ARK-mappen/VBOOT.PBP) */
int sys_is_launcher(void);

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
