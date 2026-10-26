/* psp5 – made by 08Weber */
#ifndef ARKCFG_H
#define ARKCFG_H

/* ARK:s CFW-inställningar (ARK_01234/SETTINGS.TXT), för ARK-5 och ARK-4.
 * Läses och skrivs i samma format som ARK:s egen arkMenu (Launcher-Menus/arkMenu/include/ark_settings.h). */

/* hur en inställning är aktiverad (som i arkMenu) */
enum { CFG_OFF, CFG_ALWAYS, CFG_GAME, CFG_UMD, CFG_HOMEBREW, CFG_POPS, CFG_VSH, CFG_LAUNCHER };

/* CPU-hastigheter, index i CLOCK_NAMES */
enum { CLOCK_AUTO, CLOCK_133, CLOCK_222, CLOCK_333, CLOCK_383, CLOCK_403, CLOCK_423, CLOCK_443, CLOCK_COUNT };

typedef struct {
    unsigned char usbcharge, clock_game, clock_vsh, wpa2, launcher;
    unsigned char highmem;      /* 0 av, 1 standard, 2 auto, 3 +16 MiB, 4 max */
    unsigned char mscache;      /* 0 av, 1 4k, 2 8k, 3 16k */
    unsigned char infernocache; /* 0 av, 1 LRU, 2 RR */
    unsigned char disablepause, oldplugin, hibblock;
    unsigned char skiplogos;    /* 0 av, 1 alla, 2 gameboot, 3 coldboot */
    unsigned char regionchange; /* 0 standard, 1 JP, 2 US, 3 EU */
    unsigned char vshregion;    /* 0 standard, annars fakeregion_N */
    unsigned char hidepics;     /* 0 av, 1 alla, 2 PIC0, 3 PIC1 */
    unsigned char hidemac, hidedlc, noled, noumd, deadef, noanalog, vitamute, qaflags;
} CfwConf;

extern CfwConf g_cfw;
/* 1 = läs/skriv i ARK-4:s format (sätts av sys_init): klockan som overclock/defaultclock/
 * powersave, highmem och mscache som på/av, inga deadef/vitamute */
extern int g_arkcfg_ark4;

/* Returnerar 1 om filen kunde läsas. Saknas den används ARK:s standardvärden. */
int arkcfg_load(const char *path);
int arkcfg_save(const char *path);

#endif
